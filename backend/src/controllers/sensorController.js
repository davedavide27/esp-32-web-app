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

      const { temperature1, humidity1, voltage, fanOn, motion, timestamp } = req.body;

      // Accept null values for sensor fields, only require fanOn and timestamp
      if (fanOn === undefined || timestamp === undefined) {
        return res.status(400).json({ error: 'Missing required fields: fanOn or timestamp' });
      }

      const data = {
        temperature: temperature1 == null || isNaN(temperature1) ? null : parseFloat(temperature1),
        temperature1: temperature1 == null || isNaN(temperature1) ? null : parseFloat(temperature1),
        humidity1: humidity1 == null || isNaN(humidity1) ? null : parseFloat(humidity1),
        voltage: voltage == null || isNaN(voltage) ? null : parseFloat(voltage),
        fanOn: (fanOn === true || fanOn === 'true' || fanOn === 'on') ? 1 : 0,
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
