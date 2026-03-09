const SensorData = require('../models/sensorData');
const SocketService = require('../services/socketService');
const LedState = require('../models/ledState');

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

      const { temperature1, humidity1, voltage, fanOn, motion, timestamp, button1, button2, button3, button4, ld2410cHumanPresent, led1, led2, led3 } = req.body;

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
        button1: button1 === true || button1 === 'true' ? 1 : 0,
        button2: button2 === true || button2 === 'true' ? 1 : 0,
        button3: button3 === true || button3 === 'true' ? 1 : 0,
        button4: button4 === true || button4 === 'true' ? 1 : 0,
        ld2410cHumanPresent: ld2410cHumanPresent === true || ld2410cHumanPresent === 'true' ? 1 : 0,
        led1: led1 === true || led1 === 'true' ? 1 : 0,
        led2: led2 === true || led2 === 'true' ? 1 : 0,
        led3: led3 === true || led3 === 'true' ? 1 : 0,
        timestamp: new Date() // Use server time
      };

      // Update led_states table based on button presses (manual control)
      if (button1 === true || button1 === 'true' || button1 === 1) {
        await LedState.setLedState('led1', true);
        await LedState.setLedState('led2', false);
        await LedState.setLedState('led3', false);
      } else if (button2 === true || button2 === 'true' || button2 === 1) {
        await LedState.setLedState('led1', false);
        await LedState.setLedState('led2', true);
        await LedState.setLedState('led3', false);
      } else if (button3 === true || button3 === 'true' || button3 === 1) {
        await LedState.setLedState('led1', false);
        await LedState.setLedState('led2', false);
        await LedState.setLedState('led3', true);
      } else if (button4 === true || button4 === 'true' || button4 === 1) {
        await LedState.setLedState('led1', false);
        await LedState.setLedState('led2', false);
        await LedState.setLedState('led3', false);
      }

      // Sync led_states table with ESP32 LED state from sensor data
      if (data.button1 || data.led1) {
        await LedState.setLedState('led1', true);
        await LedState.setLedState('led2', false);
        await LedState.setLedState('led3', false);
      } else if (data.button2 || data.led2) {
        await LedState.setLedState('led1', false);
        await LedState.setLedState('led2', true);
        await LedState.setLedState('led3', false);
      } else if (data.button3 || data.led3) {
        await LedState.setLedState('led1', false);
        await LedState.setLedState('led2', false);
        await LedState.setLedState('led3', true);
      } else if (data.button4) {
        await LedState.setLedState('led1', false);
        await LedState.setLedState('led2', false);
        await LedState.setLedState('led3', false);
      }

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
