/** Everyone else in the session (there are no parties); the host owns it. */
export class Group {
  owner: number;
  members: Array<number>;

  constructor(
    options: {
      owner?: number;
      members?: Array<number>;
    } = {},
  ) {
    this.owner = options.owner;
    this.members = options.members || new Array<number>();
  }
}
