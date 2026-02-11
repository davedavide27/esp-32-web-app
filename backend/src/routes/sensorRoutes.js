const express = require('express');
const router = express.Router();
const sensorController = require('../controllers/sensorController');

// GET /api/sensor-data - Get historical sensor data
router.get('/sensor-data', sensorController.getSensorData);

// POST /api/sensor-data - Receive sensor data from ESP32
router.post('/sensor-data', sensorController.receiveSensorData);

// GET /api/esp32-status - Get ESP32 connection status
router.get('/esp32-status', sensorController.getEsp32Status);

// GET /api/last-update - Get last update timestamp
router.get('/last-update', sensorController.getLastUpdate);

module.exports = router;
