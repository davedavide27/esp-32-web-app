const express = require('express');
const router = express.Router();
const sensorController = require('../controllers/sensorController');
const commandController = require('../controllers/commandController');

// GET /api/sensor-data - Get historical sensor data
router.get('/sensor-data', sensorController.getSensorData);

// POST /api/sensor-data - Receive sensor data from ESP32
router.post('/sensor-data', sensorController.receiveSensorData);

// GET /api/esp32-status - Get ESP32 connection status
router.get('/esp32-status', sensorController.getEsp32Status);

// GET /api/last-update - Get last update timestamp
router.get('/last-update', sensorController.getLastUpdate);

// POST /api/command - Set a command for ESP32 (from frontend)
router.post('/command', commandController.postCommand);

// GET /api/command - ESP32 polls this to receive a pending command
router.get('/command', commandController.getCommand);

// GET /api/led-states - Return current LED states for frontend
router.get('/led-states', commandController.getStates);

// POST /api/led-states - ESP syncs full LED state (boot/reconnect)
router.post('/led-states', commandController.postLedStates);

// POST /api/ack - ESP32 posts acknowledgement for applied LED state
router.post('/ack', commandController.postAck);

module.exports = router;
