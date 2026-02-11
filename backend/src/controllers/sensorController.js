const SensorData = require('../models/sensorData');
const SocketService = require('../services/socketService');

class SensorController {
  static async getSensorData(req, res) {
    try {
      const limit = parseInt(req.query.limit) || 100;
      const data = await SensorData.getAll(limit);
      res.json(data.reverse()); // Reverse to show oldest first
    } catch (error) {
      console.error('Error fetching sensor data:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  }

  static async getLatestSensorData(req, res) {
    try {
      const data = await SensorData.getLatest();
      if (data) {
        res.json(data);
      } else {
        res.status(404).json({ error: 'No sensor data found' });
      }
    } catch (error) {
      console.error('Error fetching latest sensor data:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  }

  static async receiveSensorData(req, res) {
    try {
      const { temperature1, humidity1, temperature2, humidity2, current, motion, timestamp } = req.body;

      // Validate data
      if (temperature1 === undefined || humidity1 === undefined || temperature2 === undefined || humidity2 === undefined || current === undefined) {
        return res.status(400).json({ error: 'Missing required sensor data fields' });
      }

      const data = {
        temperature: parseFloat(temperature1) || 0,
        temperature1: parseFloat(temperature1) || 0,
        humidity1: parseFloat(humidity1) || 0,
        temperature2: parseFloat(temperature2) || 0,
        humidity2: parseFloat(humidity2) || 0,
        current: parseFloat(current) || 0,
        motion: motion === true || motion === 'true' ? 1 : 0,
        timestamp: new Date() // Use server time
      };

      // Insert data into database only if there's a significant spike
      const savedData = await SensorData.createIfSignificantSpike(data);

      // Update ESP32 activity timestamp
      SocketService.updateEsp32Activity();

      // Emit to connected clients
      SocketService.emitSensorData(data);

      // If data was saved, emit savedSensorData to update historical data
      if (savedData) {
        SocketService.emitSavedSensorData(data);
      }

      console.log('Sensor data received from ESP32 and broadcasted:', data);
      res.status(200).json({ message: 'Data received successfully' });
    } catch (error) {
      console.error('Error saving sensor data:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  }

  static async getLastUpdate(req, res) {
    try {
      const lastUpdate = await SensorData.getLastUpdate();
      res.json({ lastUpdate: lastUpdate });
    } catch (error) {
      console.error('Error fetching last update:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  }

  static getEsp32Status(req, res) {
    const status = SocketService.getEsp32Status();
    res.json({ status });
  }
}

module.exports = SensorController;
