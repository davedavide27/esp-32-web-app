const SocketService = require('../services/socketService');

class CommandController {
  static async postCommand(req, res) {
    try {
      const { action } = req.body;
      if (!action) return res.status(400).json({ error: 'Missing action' });

      // Accept simple actions like 'led_on' and 'led_off'
      SocketService.setCommand(action);
      // Notify connected frontends
      if (SocketService.io) SocketService.io.emit('commandUpdate', { action });

      console.log('Command set:', action);
      res.json({ message: 'Command set' });
    } catch (error) {
      console.error('Error setting command:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  }

  static async getCommand(req, res) {
    try {
      const cmd = SocketService.getAndClearCommand();
      res.json({ command: cmd || null });
    } catch (error) {
      console.error('Error getting command:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  }

  static async postAck(req, res) {
    try {
      const { led, state } = req.body;
      if (!led || typeof state === 'undefined') {
        return res.status(400).json({ error: 'led and state required' });
      }

      if (SocketService.ledStates && Object.prototype.hasOwnProperty.call(SocketService.ledStates, led)) {
        const newState = !!state;
        SocketService.ledStates[led] = newState;

        // Mutual exclusion: if a LED is turned ON, turn off all others
        if (newState) {
          Object.keys(SocketService.ledStates).forEach(key => {
            if (key !== led) {
              SocketService.ledStates[key] = false;
            }
          });
        }

        // Persist all LED states to database
        for (const [ledKey, ledState] of Object.entries(SocketService.ledStates)) {
          await SocketService.persistLedState(ledKey, ledState);
        }

        // Emit updated states to all clients
        SocketService.emitLedStates();
        return res.json({ message: 'ack received' });
      }

      return res.status(400).json({ error: 'invalid led id' });
    } catch (error) {
      console.error('Error processing ack:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  }

  static async getStates(req, res) {
    try {
      const states = SocketService.getLedStates();
      res.json({ states });
    } catch (error) {
      console.error('Error getting LED states:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  }

  // Endpoint for ESP to report full LED state (e.g., at boot or after reconnect)
  static async postLedStates(req, res) {
    try {
      const { states } = req.body;
      if (!states || typeof states !== 'object') {
        return res.status(400).json({ error: 'states object required' });
      }

      // Update in-memory states
      Object.keys(SocketService.ledStates).forEach(key => {
        if (key in states) {
          SocketService.ledStates[key] = !!states[key];
        }
      });

      // Persist to database
      for (const [ledKey, ledState] of Object.entries(SocketService.ledStates)) {
        await SocketService.persistLedState(ledKey, ledState);
      }

      // Emit to clients
      SocketService.emitLedStates();
      console.log('LED states synced from ESP:', SocketService.ledStates);
      return res.json({ message: 'states synced' });
    } catch (error) {
      console.error('Error syncing LED states:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  }
}

module.exports = CommandController;
