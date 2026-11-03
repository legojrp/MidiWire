const { WebSocketServer } = require('ws');

const PORT = process.env.PORT || 8080;
const wss = new WebSocketServer({ port: PORT });
const rooms = new Map();

wss.on('connection', (ws) => {
  let currentRoom = null;

  ws.on('message', (message) => {
    try {
      const data = JSON.parse(message.toString());

      if (data.type === 'join') {
        currentRoom = data.roomId || data.room;
        if (!rooms.has(currentRoom)) {
          rooms.set(currentRoom, new Set());
        }

        const roomClients = rooms.get(currentRoom);
        roomClients.add(ws);
        console.log(`[+] Client joined room: ${currentRoom} (Total: ${roomClients.size})`);

        // NOTIFY EXISTING PEERS: Tell other clients in the room to initiate a WebRTC offer
        for (const client of roomClients) {
          if (client !== ws && client.readyState === ws.OPEN) {
            client.send(JSON.stringify({ type: 'peer_joined' }));
          }
        }
        return;
      }

      // Relay WebRTC signals (offer, answer, candidate) to all other peers in the room
      if (currentRoom && rooms.has(currentRoom)) {
        for (const client of rooms.get(currentRoom)) {
          if (client !== ws && client.readyState === ws.OPEN) {
            client.send(JSON.stringify(data));
          }
        }
      }
    } catch (err) {
      console.error('[-] Error processing message:', err.message);
    }
  });

  ws.on('close', () => {
    if (currentRoom && rooms.has(currentRoom)) {
      const room = rooms.get(currentRoom);
      room.delete(ws);
      console.log(`[-] Client disconnected from room: ${currentRoom}`);

      // Optional: Notify remaining peer that the connection was closed
      for (const client of room) {
        if (client.readyState === ws.OPEN) {
          client.send(JSON.stringify({ type: 'peer_left' }));
        }
      }

      if (room.size === 0) {
        rooms.delete(currentRoom);
      }
    }
  });
});

console.log(`[MidiWire Signaling Server] Listening on ws://localhost:${PORT}`);