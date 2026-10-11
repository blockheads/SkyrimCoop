import { ChangeDetectionStrategy, Component } from '@angular/core';
import { Observable, pluck, ReplaySubject, share } from 'rxjs';
import { map } from 'rxjs/operators';
import { ClientService } from 'src/app/services/client.service';
import { PlayerListService } from 'src/app/services/player-list.service';
import { Player } from '../../models/player';

@Component({
  selector: 'app-player-list',
  templateUrl: './player-list.component.html',
  styleUrls: ['./player-list.component.scss'],
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class PlayerListComponent {
  playerList$: Observable<Player[]>;
  playerListLength$: Observable<number>;

  constructor(
    private readonly playerListService: PlayerListService,
    private readonly clientService: ClientService,
  ) {
    this.playerList$ = this.playerListService.playerList.asObservable().pipe(
      pluck('players'),
      map(players => players ?? []),
      share({
        connector: () => new ReplaySubject(1),
        resetOnRefCountZero: true,
      }),
    );
    this.playerListLength$ = this.playerList$.pipe(
      map(players => players.length),
    );
  }

  /** The host alone may remove a player from the session. */
  get isHost(): boolean {
    return this.clientService.isHost;
  }

  public kick(playerId: number) {
    this.playerListService.kick(playerId);
  }
}
