#include <StdInc.h>
#include "Console.Commands.h"
#include "Console.Variables.h"
#include "Console.h"

#include <sstream>

namespace console
{
struct ConsoleManagers : public ConsoleManagersBase
{
	std::unique_ptr<ConsoleCommandManager> commandManager;

	std::unique_ptr<ConsoleVariableManager> variableManager;

	std::shared_ptr<ConsoleCommand> helpCommand;
};

Context::Context()
    : Context(GetDefaultContext())
{
}

Context::Context(Context* fallbackContext)
    : m_fallbackContext(fallbackContext)
{
	m_managers = std::make_unique<ConsoleManagers>();

	ConsoleManagers* managers = static_cast<ConsoleManagers*>(m_managers.get());
	managers->commandManager  = std::make_unique<ConsoleCommandManager>(this);
	managers->variableManager = std::make_unique<ConsoleVariableManager>(this);

	managers->helpCommand = std::make_shared<ConsoleCommand>(managers->commandManager.get(), "help", [=]()
	{
		std::set<std::string> commands;

		managers->commandManager->ForAllCommands([&](const std::string& cmdName)
		{
			commands.insert(cmdName);
		});

		for (auto& commandName : commands)
		{
			auto cvar = managers->variableManager->FindEntryRaw(commandName);

			if (cvar)
			{
				console::Printf("CmdSystem", "%s = %s\n", commandName, cvar->GetValue());
			}
			else
			{
				console::Printf("CmdSystem", "%s\n", commandName);
			}
		}
	});

	m_variableModifiedFlags = 0;
}

ConsoleVariableManager* Context::GetVariableManager()
{
	return static_cast<ConsoleManagers*>(m_managers.get())->variableManager.get();
}

ConsoleCommandManager* Context::GetCommandManager()
{
	return static_cast<ConsoleManagers*>(m_managers.get())->commandManager.get();
}

void Context::ExecuteSingleCommand(const std::string& command)
{
	ProgramArguments arguments = Tokenize(command);

	ExecuteSingleCommand(arguments);
}

void Context::ExecuteSingleCommand(const ProgramArguments& arguments)
{
	// early out if no command nor arguments were passed
	if (arguments.Count() == 0)
	{
		return;
	}

	// make a copy of the arguments to shift off the command name
	ProgramArguments localArgs(arguments);
	std::string command = localArgs.Shift();

	// run the command through the command manager
	GetCommandManager()->Invoke(command, localArgs);
}

void Context::AddToBuffer(const std::string& text)
{
	std::lock_guard<std::mutex> guard(m_commandBufferMutex);
	m_commandBuffer += text;
}

void Context::ExecuteBuffer()
{
	std::vector<std::string> toExecute;

	{
		std::lock_guard<std::mutex> guard(m_commandBufferMutex);

		while (m_commandBuffer.length() > 0)
		{
			// parse the command up to the first occurrence of a newline/semicolon
			int i        = 0;
			bool inQuote = false;

			size_t cbufLength = m_commandBuffer.length();

			for (i = 0; i < cbufLength; i++)
			{
				if (m_commandBuffer[i] == '"')
				{
					inQuote = !inQuote;
				}

				// break if a semicolon
				if (!inQuote && m_commandBuffer[i] == ';')
				{
					break;
				}

				// or a newline
				if (m_commandBuffer[i] == '\r' || m_commandBuffer[i] == '\n')
				{
					break;
				}
			}

			std::string command = m_commandBuffer.substr(0, i);

			if (cbufLength > i)
			{
				m_commandBuffer = m_commandBuffer.substr(i + 1);
			}
			else
			{
				m_commandBuffer.clear();
			}

			// and add the command for execution when the mutex is unlocked
			toExecute.push_back(command);
		}
	}

	for (const std::string& command : toExecute)
	{
		ExecuteSingleCommand(command);
	}
}

static void SaveConfiguration(const std::string& path, ConsoleVariableManager* varMan)
{

}

void Context::SaveConfigurationIfNeeded(const std::string& path)
{
	// check if the configuration was saved already
	static bool wasSavedBefore = false;

	// mark a flag to see if any variables are modified (or if we haven't done our initial save)
	if (!wasSavedBefore || (m_variableModifiedFlags & ConVar_Archive))
	{
		console::DPrintf("cmd", "Saving configuration to %s...\n", path.c_str());

		SaveConfiguration(path, GetVariableManager());

		wasSavedBefore = true;

		m_variableModifiedFlags &= ~ConVar_Archive;
	}
}

void Context::SetVariableModifiedFlags(int flags)
{
	m_variableModifiedFlags |= flags;
}

// default context functions
Context* GetDefaultContext()
{
	static std::unique_ptr<Context> defaultContext;
	static std::once_flag flag;

	std::call_once(flag, []() {
		// nullptr is important - we don't have ourselves to fall back on!
		defaultContext = std::make_unique<Context>(nullptr);
	});

	return defaultContext.get();
}

void CreateContext(Context* parentContext, fwRefContainer<Context>* outContext)
{
	*outContext = new Context(parentContext);
}

void ExecuteSingleCommand(const std::string& command)
{
	return GetDefaultContext()->ExecuteSingleCommand(command);
}

void ExecuteSingleCommand(const ProgramArguments& arguments)
{
	return GetDefaultContext()->ExecuteSingleCommand(arguments);
}

void AddToBuffer(const std::string& text)
{
	return GetDefaultContext()->AddToBuffer(text);
}

void ExecuteBuffer()
{
	return GetDefaultContext()->ExecuteBuffer();
}

void SaveConfigurationIfNeeded(const std::string& path)
{
	return GetDefaultContext()->SaveConfigurationIfNeeded(path);
}

void SetVariableModifiedFlags(int flags)
{
	return GetDefaultContext()->SetVariableModifiedFlags(flags);
}

static inline bool IsEscapeChar(char c)
{
	return (c == '"');
}

ProgramArguments Tokenize(const std::string& line)
{
	int i = 0;
	int j = 0;
	std::vector<std::string> args;

	size_t lineLength = line.length();

	// outer loop
	while (true)
	{
		// inner loop to skip whitespace
		while (true)
		{
			// skip whitespace and control characters
			while (i < lineLength && line[i] <= ' ') // ASCII only?
			{
				i++;
			}

			// return if needed
			if (i >= lineLength)
			{
				return ProgramArguments{args};
			}

			// allegedly fixes issues with parsing
			if (i == 0)
			{
				break;
			}

			// skip comments
			if ((line[i] == '/' && line[i + 1] == '/') || line[i] == '#') // full line is a comment
			{
				return ProgramArguments{args};
			}

			// /* comments
			if (line[i] == '/' && line[i + 1] == '*')
			{
				while (i < (lineLength + 1) && (line[i] != '*' && line[i + 1] != '/'))
				{
					i++;
				}

				if (i >= lineLength)
				{
					return ProgramArguments{args};
				}

				i += 2;
			}
			else
			{
				break;
			}
		}

		// there's a new argument on the block
		std::stringstream arg;

		// quoted strings
		if (line[i] == '"')
		{
			bool inEscape = false;

			while (true)
			{
				i++;

				if (i >= lineLength)
				{
					break;
				}

				if (line[i] == '"' && !inEscape)
				{
					break;
				}

				if (line[i] == '\\' && IsEscapeChar(line[i + 1]))
				{
					inEscape = true;
				}
				else
				{
					arg << line[i];
					inEscape = false;
				}
			}

			i++;

			args.push_back(arg.str());
			j++;

			if (i >= lineLength)
			{
				return ProgramArguments{args};
			}

			continue;
		}

		// non-quoted strings
		while (i < lineLength && line[i] > ' ')
		{
			if (line[i] == '"')
			{
				break;
			}

			// # comments are one character long
			if (i < lineLength)
			{
				if (line[i] == '#')
				{
					return ProgramArguments{args};
				}
			}

			if (i < (lineLength - 1))
			{
				if ((line[i] == '/' && line[i + 1] == '/'))
				{
					return ProgramArguments{args};
				}

				if (line[i] == '/' && line[i + 1] == '*')
				{
					return ProgramArguments{args};
				}
			}

			arg << line[i];

			i++;
		}

		std::string argStr = arg.str();

		if (!argStr.empty())
		{
			args.push_back(argStr);
			j++;
		}

		if (i >= lineLength)
		{
			return ProgramArguments{args};
		}
	}
}
}

void CoreTrace(const char* channel, const char* func, const char* file, int line, const char* string)
{
	console::Printf(channel, "%s", string);
}

static InitFunction initFunction([]()
{
	auto cxt = console::GetDefaultContext();
	Instance<ConsoleCommandManager>::Set(cxt->GetCommandManager());
	Instance<ConsoleVariableManager>::Set(cxt->GetVariableManager());
	Instance<console::Context>::Set(cxt);
});
