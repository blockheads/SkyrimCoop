# Network Bridge

This is the **host-side** P2P networking component used when running as the authoritative host.

**Role:** 
- SEND authoritative game state to all connected peers
- RECEIVE input/requests from remote clients  
- Broadcast world updates directly (no server relay)

**Used by:** The host client only