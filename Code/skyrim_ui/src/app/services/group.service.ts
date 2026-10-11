import { Injectable, OnDestroy } from '@angular/core';
import { combineLatest, BehaviorSubject, Observable, Subscription } from 'rxjs';
import { filter, map, pluck } from 'rxjs/operators';
import { Group } from '../models/group';
import { Player } from '../models/player';
import { ClientService } from './client.service';
import { ChatService } from './chat.service';
import { PlayerListService } from './player-list.service';

/**
 * The group panel's players: everyone else in the session (there are no parties), the host as
 * its owner. Undefined while not connected.
 */
@Injectable({
  providedIn: 'root',
})
export class GroupService implements OnDestroy {
  public group = new BehaviorSubject<Group | undefined>(undefined);

  private sessionSubscription: Subscription;
  private userHealthSubscription: Subscription;
  private levelSubscription: Subscription;
  private loadedSubscription: Subscription;

  constructor(
    private readonly clientService: ClientService,
    private readonly chatService: ChatService,
    private readonly playerListService: PlayerListService,
  ) {
    this.onSessionChange();
    this.subscribeChangeHealth();
    this.onLevelChange();
    this.onLoadedChange();
  }

  ngOnDestroy() {
    this.sessionSubscription.unsubscribe();
    this.userHealthSubscription.unsubscribe();
    this.levelSubscription.unsubscribe();
    this.loadedSubscription.unsubscribe();
  }

  private onSessionChange() {
    this.sessionSubscription = combineLatest([
      this.clientService.connectionStateChange,
      this.playerListService.playerList,
    ]).subscribe(([connected, playerList]) => {
      if (!connected) {
        this.group.next(undefined);
        return;
      }

      const players = playerList?.players ?? [];
      const host = players.find(player => player.isHost);
      this.group.next(
        new Group({
          owner: host ? host.id : this.clientService.localPlayerId,
          members: players.map(player => player.id),
        }),
      );
    });
  }

  private subscribeChangeHealth() {
    this.userHealthSubscription = this.clientService.healthChange.subscribe(
      (player: Player) => {
        const foundPlayer = this.playerListService.getPlayerById(player.id);
        if (foundPlayer) {
          foundPlayer.health = player.health;
          this.playerListService.updatePlayerList();
        }
      },
    );
  }

  private onLevelChange() {
    this.levelSubscription = this.clientService.levelChange.subscribe(
      (player: Player) => {
        const p = this.playerListService.getPlayerById(player.id);
        if (p) {
          p.level = player.level;
          this.chatService.pushSystemMessage('SERVICE.GROUP.LEVEL_UP', {
            name: p.name,
            level: player.level,
          });
        }
      },
    );
  }

  private onLoadedChange() {
    this.loadedSubscription = this.clientService.isLoadedChange.subscribe(
      (player: Player) => {
        const p = this.playerListService.getPlayerById(player.id);
        if (p) {
          p.isLoaded = player.isLoaded;
          p.health = player.health;
          this.playerListService.updatePlayerList();
        }
      },
    );
  }

  public selectMembers(): Observable<Player[]> {
    return this.playerListService.playerList.asObservable().pipe(
      filter(playerList => !!playerList),
      pluck('players'),
    );
  }
}
