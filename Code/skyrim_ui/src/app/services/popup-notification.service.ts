import { Injectable } from '@angular/core';
import { Subject } from 'rxjs';
import { PopupNotification } from '../models/popup-notification';
import { Sound, SoundService } from './sound.service';

@Injectable({
  providedIn: 'root',
})
export class PopupNotificationService {
  private message = new Subject<PopupNotification>();
  public message$ = this.message.asObservable();
  private messagesCleared = new Subject<void>();
  public messagesCleared$ = this.messagesCleared.asObservable();

  constructor(private readonly soundService: SoundService) {}

  public addMessage(notification: PopupNotification) {
    this.message.next(notification);
    this.soundService.play(Sound.Focus);
  }

  public clearMessages() {
    this.messagesCleared.next();
  }
}
