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
        currentRoom = data.roomId;
        if (!rooms.has(currentRoom)) {
          rooms.set(currentRoom, new Set());
        }
        rooms.get(currentRoom).add(ws);
        console.log(`[+] Client joined room: ${currentRoom} (Total: ${rooms.get(currentRoom).size})`);
        return;
      }

      // Relay signal to all other clients in the same room
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
      if (room.size === 0) {
        rooms.delete(currentRoom);
      }
    }
  });
});

console.log(`[MidiWire Signaling Server] Listening on ws://localhost:${PORT}`);