export interface Friend {
  id: number;
  name: string;
  avatar: string;
  online: boolean;
}

/** Player status. */
export class Player implements Friend {
  /** Player ID. */
  id: number;

  /** Username. */
  name: string;

  avatar: string;

  /** Current health. */
  health?: number;

  /** Level */
  level?: number;

  /** Online status. */
  online: boolean;

  /** Connected in server. */
  connected: boolean;

  /** CellName */
  cellName: string;

  /** isLoaded */
  isLoaded: boolean;

  /** The session's host. */
  isHost: boolean;

  constructor(
    options: {
      id?: number;
      name?: string;
      avatar?: string;
      online?: boolean;
      connected?: boolean;
      health?: number;
      level?: number;
      cellName?: string;
      isLoaded?: boolean;
      isHost?: boolean;
    } = {},
  ) {
    this.id = options.id || 0;
    this.name = options.name || '';
    this.avatar = options.avatar || '';
    this.cellName = options.cellName || 'vide';

    if (options.health || options.health === 0) {
      this.health = options.health;
    } else {
      this.health = undefined;
    }

    if (options.level || options.level === 0) {
      this.level = options.level;
    } else {
      this.level = undefined;
    }

    if (options.online === undefined || options.online === null) {
      this.online = false;
    } else {
      this.online = options.online;
    }

    if (options.connected === undefined || options.connected === null) {
      this.connected = false;
    } else {
      this.connected = options.connected;
    }


    if (options.isLoaded === undefined || options.isLoaded === null) {
      this.isLoaded = false;
    } else {
      this.isLoaded = options.isLoaded;
    }

    this.isHost = options.isHost || false;
  }
}
