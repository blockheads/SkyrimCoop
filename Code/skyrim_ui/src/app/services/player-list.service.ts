import { Injectable, OnDestroy } from '@angular/core';
import { BehaviorSubject, Subscription } from 'rxjs';
import { Player } from '../models/player';
import { PlayerList } from '../models/player-list';
import { ClientService } from './client.service';

/** The other players in the session (everyone connected is in it). */
@Injectable({
  providedIn: 'root',
})
export class PlayerListService implements OnDestroy {
  public playerList = new BehaviorSubject<PlayerList | undefined>(undefined);

  private debugSubscription: Subscription;
  private connectionSubscription: Subscription;
  private playerConnectedSubscription: Subscription;
  private playerDisconnectedSubscription: Subscription;
  private cellSubscription: Subscription;

  private isConnect = false;

  constructor(private readonly clientService: ClientService) {
    this.onDebug();
    this.onConnectionStateChanged();
    this.onPlayerConnected();
    this.onPlayerDisconnected();
    this.onCellChange();
  }

  ngOnDestroy() {
    this.debugSubscription.unsubscribe();
    this.connectionSubscription.unsubscribe();
    this.playerConnectedSubscription.unsubscribe();
    this.playerDisconnectedSubscription.unsubscribe();
    this.cellSubscription.unsubscribe();
  }

  private onDebug() {
    this.debugSubscription = this.clientService.debugChange.subscribe(() => {
      console.log(this.playerList);
    });
  }

  private onConnectionStateChanged() {
    this.connectionSubscription =
      this.clientService.connectionStateChange.subscribe((connect: boolean) => {
        if (this.isConnect == connect) {
          return;
        }
        this.isConnect = connect;
        this.playerList.next(undefined);

        this.updatePlayerList();
      });
  }

  private onPlayerConnected() {
    this.playerConnectedSubscription =
      this.clientService.playerConnectedChange.subscribe((player: Player) => {
        const playerList = this.getPlayerList();

        if (playerList) {
          playerList.players.push(player);

          this.playerList.next(playerList);
        }
      });
  }

  private onPlayerDisconnected() {
    this.playerDisconnectedSubscription =
      this.clientService.playerDisconnectedChange.subscribe(
        (playerDisco: Player) => {
          const playerList = this.getPlayerList();

          if (playerList) {
            playerList.players = playerList.players.filter(
              player => player.id !== playerDisco.id,
            );

            this.playerList.next(playerList);
          }
        },
      );
  }

  private onCellChange() {
    this.cellSubscription = this.clientService.cellChange.subscribe(
      (player: Player) => {
        const playerList = this.getPlayerList();

        if (playerList) {
          const p = this.getPlayerById(player.id);
          if (p) {
            p.cellName = player.cellName;
          }
        }
      },
    );
  }

  public getLocalPlayer(): Player {
    let localPlayerId = this.clientService.localPlayerId;
    return this.getPlayerById(localPlayerId);
  }

  public getPlayerList() {
    return this.createPlayerList(this.playerList.getValue());
  }

  public getListLength(): number {
    return this.getPlayerList() ? this.getPlayerList().players.length : 0;
  }

  private createPlayerList(playerList: PlayerList | undefined) {
    if (!playerList) {
      playerList = new PlayerList();
      this.playerList.next(playerList);
    }
    return this.playerList.getValue();
  }

  public updatePlayerList() {
    this.playerList.next(this.playerList.getValue());
  }

  /** As the host, removes the player from the session. */
  public kick(playerId: number) {
    if (this.clientService.isHost) {
      this.clientService.kickPlayer(playerId);
    }
  }

  public getPlayerById(playerId: number): Player {
    return this.getPlayerList().players.find(player => player.id === playerId);
  }
}
