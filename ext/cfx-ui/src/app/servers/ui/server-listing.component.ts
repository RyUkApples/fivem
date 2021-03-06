import { Component, OnInit, OnChanges, Input } from '@angular/core';
import { Server, PinConfig } from '../server';

import { ServerHeadingColumn } from './server-heading.component';
import { ServerFilters } from './server-filter.component';

import {Subject} from 'rxjs/Rx';

@Component({
    moduleId: module.id,
    selector: 'app-server-listing',
    templateUrl: 'server-listing.component.html',
    styleUrls: ['server-listing.component.scss']
})

export class ServerListingComponent implements OnInit, OnChanges {
    @Input()
    private servers: Server[];

    @Input()
    private filters: ServerFilters;

    @Input()
    private pinConfig: PinConfig;

    private subscriptions: { [addr: string]: any } = {};

    sortOrder: string[];

    get columns(): ServerHeadingColumn[] {
        return [
            {
                column: 'icon',
                label: ''
            },
            {
                column: 'name',
                label: '#ServerList_Name'
            },
            {
                column: 'players',
                label: '#ServerList_Players'
            },
            {
                column: 'ping',
                label: '#ServerList_Ping'
            }
        ];
    }

    localServers: Server[];
    sortedServers: Server[];

    constructor() {
        this.servers = [];

        const storedOrder = localStorage.getItem('sortOrder');

        if (storedOrder) {
            this.sortOrder = JSON.parse(storedOrder);
        } else {
            this.sortOrder = ['ping', '+'];
        }

        this.changeObservable.throttleTime(1000).subscribe(() => {
            this.sortAndFilterServers();
        });
    }

    isPinned(server: Server) {
        if (!this.pinConfig || !this.pinConfig.pinnedServers) {
            return false;
        }

        return (this.pinConfig.pinnedServers.indexOf(server.address) >= 0)
    }

    private static quoteRe(text: string) {
        return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    }

    private buildSearchMatch(filters: ServerFilters) {
        const searchText = filters.searchText;
        const filterFns: ((server: Server) => boolean)[] = [];

        const searchRe = /((?:~?\/.*?\/)|(?:[^\s]+))\s?/g;
        const categoryRe = /^([^:]*?):(.*)$/

        let match: RegExpExecArray;

        while (match = searchRe.exec(searchText)) {
            let searchGroup = match[1];
            let invertSearch = false;

            if (searchGroup.startsWith('~')) {
                searchGroup = searchGroup.substring(1);
                invertSearch = true;
            }

            if (searchGroup.length < 2) {
                continue;
            }

            const categoryMatch = searchGroup.match(categoryRe);
            
            if (!categoryMatch) {
                const reString = 
                    (searchGroup.match(/^\/(.+)\/$/)) ?
                        searchGroup.replace(/^\/(.+)\/$/, '$1')
                    :
                        ServerListingComponent.quoteRe(searchGroup);

                try {
                    const re = new RegExp(reString, 'i');
                    filterFns.push(a => (invertSearch) ? !re.test(a.strippedname) : re.test(a.strippedname));
                } catch (e) {}
            } else {
                const category = categoryMatch[1];
                const match = new RegExp(ServerListingComponent.quoteRe(categoryMatch[2]), 'i');

                filterFns.push(server =>
                {
                    let result = false;

                    if (server.data[category]) {
                        result = match.test(String(server.data[category]));
                    } else if (server.data[category + 's']) {
                        const fields = server.data[category + 's'];

                        if (Array.isArray(fields)) {
                            result = fields.filter(a => match.test(String(a))).length > 0;
                        }
                    }

                    if (invertSearch) {
                        result = !result;
                    }

                    return result;
                });
            }
        }
        
        return (server: Server) =>
        {
            for (const fn of filterFns) {
                if (!fn(server)) {
                    return false;
                }
            }

            return true;
        };
    }

    getFilter(filters: ServerFilters): (server: Server) => boolean {
        const nameMatchCallback = this.buildSearchMatch(filters);

        return (server) => {
            if (!nameMatchCallback(server)) {
                return false;
            }

            if (server.currentPlayers == 0 && filters.hideEmpty) {
                if (!this.isPinned(server) || !this.pinConfig.pinIfEmpty) {
                    return false;
                }
            }

            if (server.currentPlayers >= server.maxPlayers && filters.hideFull) {
                return false;
            }

            if (filters.maxPing > 0 && server.ping >= filters.maxPing && server.ping != 9999) {
                return false;
            }

            return true;
        }
    }

    sortAndFilterServers() {
        const servers = (this.servers || []).concat().filter(this.getFilter(this.filters));

        servers.sort((a, b) => {
            const sortChain = (...stack: ((a: Server, b: Server) => number)[]) => {
                for (const entry of stack) {
                    const retval = entry(a, b);

                    if (retval != 0) {
                        return retval;
                    }
                }

                return 0;
            };

            const sortSortable = (sortable: string[]) => {
                const name = sortable[0];
                const invert = (sortable[1] == '-');

                const sort = (a: Server, b: Server) => {
                    const val1 = a.getSortable(name);
                    const val2 = b.getSortable(name);

                    if (val1 > val2) {
                        return 1;
                    }

                    if (val1 < val2) {
                        return -1;
                    }

                    return 0;
                };

                if (invert) {
                    return (a: Server, b: Server) => -(sort(a, b));
                } else {
                    return sort;
                }
            };

            return sortChain(
                (a: Server, b: Server) => {
                    const aPinned = this.isPinned(a);
                    const bPinned = this.isPinned(b);

                    if (aPinned === bPinned) {
                        return 0;
                    } else if (aPinned && !bPinned) {
                        return -1;
                    } else if (!aPinned && bPinned) {
                        return 1;
                    }
                },
                sortSortable(this.sortOrder),
                sortSortable(['ping', '+']),
                sortSortable(['name', '+'])
            );
        });

        this.sortedServers = servers;

        window.localStorage.setItem('sortOrder', JSON.stringify(this.sortOrder));
    }

    updateSorting(column: string) {
        if (this.sortOrder[0] != column) {
            this.sortOrder = [column, '+'];
        } else {
            this.sortOrder = [
                column,
                this.sortOrder[1] == '+' ? '-' : '+'
            ];
        }

        this.sortAndFilterServers();
    }

    ngOnInit() { }

    changeSubject: Subject<void> = new Subject<void>();
    changeObservable = this.changeSubject.asObservable();

    ngOnChanges() {
        for (const server of (this.servers || [])) {
            if (!this.subscriptions[server.address]) {
                this.subscriptions[server.address] = server.onChanged.subscribe(a => this.changeSubject.next());
            }
        }

        this.sortAndFilterServers();
        this.changeSubject.next();
    }
}
