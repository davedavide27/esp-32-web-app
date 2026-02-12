const express = require('express');
const http = require('http');
const socketIo = require('socket.io');
const cors = require('cors');
const db = require('./config/database');
const sensorRoutes = require('./src/routes/sensorRoutes');

const app = express();
const server = http.createServer(app);
const io = socketIo(server, {
  cors: {
    origin: "*",
    methods: ["GET", "POST"]
  }
});

// Set io instance in SocketService
const SocketService = require('./src/services/socketService');
SocketService.setIo(io);

app.use(cors());
app.use(express.json());

// Initialize LED states from database
SocketService.initializeLedStates().then(() => {
  console.log('LED states initialized from database');
}).catch(err => {
  console.error('Failed to initialize LED states:', err);
});

// Use sensor routes
app.use('/api', sensorRoutes);

// REST API route for historical data
app.get('/api/sensor-data', async (req, res) => {
  try {
    const data = await db('sensor_data').orderBy('timestamp', 'desc').limit(100);
    res.json(data.reverse()); // Reverse to show oldest first
  } catch (error) {
    console.error('Error fetching sensor data:', error);
    res.status(500).json({ error: 'Internal server error' });
  }
});

// WebSocket connection handling
SocketService.handleConnection(io);

const PORT = process.env.PORT || 5000;
server.listen(PORT, '0.0.0.0', () => {
  console.log(`Server running on port ${PORT}`);
  console.log(`Access from local network: http://<your-ip>:${PORT}`);
});
