const db = require('../../config/database');

class SensorData {
  static async getAll(limit = 100) {
    return await db('sensor_data').orderBy('timestamp', 'desc').limit(limit);
  }

  static async create(data) {
    // Only insert fields that exist in the table
    const insertData = {
      temperature: data.temperature,
      humidity1: data.humidity1,
      voltage: data.voltage,
      fanOn: data.fanOn,
      timestamp: data.timestamp || new Date()
    };
    const result = await db('sensor_data').insert(insertData);
    await this.updateLastUpdate();
    return result;
  }

  static async getLatest() {
    return await db('sensor_data').orderBy('timestamp', 'desc').first();
  }

  static async hasSignificantSpike(currentData) {
    const latest = await this.getLatest();
    if (!latest) {
      // First reading, always save
      return true;
    }

    // If no data in the last 30 minutes, always save the next reading
    const timeSinceLast = new Date() - new Date(latest.timestamp);
    if (timeSinceLast > 30 * 60 * 1000) {
      return true;
    }

    const thresholds = {
      temperature: 2.0, // 2°C change
      humidity1: 5.0,   // 5% change
      voltage: 2.0      // 2V change
    };

    const currentTemp = parseFloat(currentData.temperature);
    const baselineTemp = parseFloat(latest.temperature);
    const tempChange = Math.abs(currentTemp - baselineTemp);

    const currentHum1 = parseFloat(currentData.humidity1);
    const baselineHum1 = parseFloat(latest.humidity1);
    const hum1Change = Math.abs(currentHum1 - baselineHum1);

    const currentVolt = parseFloat(currentData.voltage);
    const baselineVolt = parseFloat(latest.voltage);
    const voltChange = Math.abs(currentVolt - baselineVolt);

    // Check if any sensor has significant change
    return tempChange >= thresholds.temperature ||
           hum1Change >= thresholds.humidity1 ||
           voltChange >= thresholds.voltage;
  }

  static async createIfSignificantSpike(data) {
    const hasSpike = await this.hasSignificantSpike(data);
    if (hasSpike) {
      const savedData = await this.create(data);
      return savedData;
    }
    return null; // No spike, don't save
  }

  static async updateLastUpdate() {
    await db('last_update').insert({ id: 1, last_updated: db.fn.now() }).onConflict('id').merge();
  }

  static async getLastUpdate() {
    const result = await db('last_update').where('id', 1).first();
    return result ? result.last_updated : null;
  }
}

module.exports = SensorData;
