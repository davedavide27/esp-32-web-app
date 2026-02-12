const SensorData = require('../models/sensorData');
const db = require('../../config/database');

class SocketService {
  static esp32LastPing = null;
  static currentCommand = null;
  static ledStates = { led1: false, led2: false, led3: false };
  static ioInstance = null;

  static handleConnection(io) {
    io.on('connection', (socket) => {
      // Only log frontend client connections, not ESP32 (ESP32 uses HTTP)
      console.log('Frontend client connected:', socket.id);

      socket.on('sensor-data', async (data) => {
        try {
          // Validate data
          if (!data.temperature || !data.voltage) {
            socket.emit('error', { message: 'Invalid sensor data' });
            return;
          }

          // Insert data into database
          await SensorData.create(data);

          // Broadcast to all clients
          io.emit('sensor-data', data);
          console.log('Sensor data received and broadcasted:', data);
        } catch (error) {
          console.error('Error saving sensor data:', error);
          socket.emit('error', { message: 'Failed to save sensor data' });
        }
      });

      socket.on('disconnect', () => {
        console.log('Frontend client disconnected:', socket.id);
      });
    });
  }

  static isEsp32Active() {
    if (!this.esp32LastPing) return false;
    const now = new Date();
    const diff = now - this.esp32LastPing;
    return diff < 30000; // Active if ping within last 30 seconds
  }

  static setIo(ioInstance) {
    this.io = ioInstance;
    this.ioInstance = ioInstance;
  }

  static async initializeLedStates() {
    try {
      // Load LED states from database
      const states = await db('led_states').select('*');
      states.forEach(row => {
        this.ledStates[row.led] = !!row.state;
      });
      console.log('LED states loaded from database:', this.ledStates);
    } catch (error) {
      console.error('Error loading LED states from database:', error);
      // Fall back to defaults
      this.ledStates = { led1: false, led2: false, led3: false };
    }
  }

  static emitSensorData(data) {
    if (this.io) {
      this.io.emit('sensorData', data);
    }
  }

  static emitSavedSensorData(data) {
    if (this.io) {
      this.io.emit('savedSensorData', data);
    }
  }

  static setCommand(cmd) {
    // Only set the pending command here. LED state will be updated when ESP32 acknowledges.
    this.currentCommand = cmd;
    // Notify frontends that a command was set
    if (this.io) this.io.emit('commandUpdate', { action: cmd });
  }

  static getAndClearCommand() {
    const cmd = this.currentCommand;
    this.currentCommand = null;
    return cmd;
  }

  static getLedStates() {
    return this.ledStates;
  }

  static async persistLedState(led, state) {
    try {
      await db('led_states')
        .insert({ led, state, updated_at: new Date() })
        .onConflict('led')
        .merge({ state, updated_at: new Date() });
      console.log(`LED state persisted: ${led} = ${state}`);
    } catch (error) {
      console.error('Error persisting LED state:', error);
    }
  }

  static emitLedStates() {
    if (this.io) {
      this.io.emit('ledStates', this.ledStates);
    }
  }

  static updateEsp32Activity() {
    this.esp32LastPing = new Date();
  }

  static getEsp32Status() {
    return {
      active: this.isEsp32Active(),
      lastPing: this.esp32LastPing
    };
  }
}

module.exports = SocketService;
