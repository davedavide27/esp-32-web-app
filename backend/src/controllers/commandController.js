const SocketService = require('../services/socketService');

class CommandController {
  static async postCommand(req, res) {
    try {
      const { action } = req.body;
      if (!action) return res.status(400).json({ error: 'Missing action' });

      // Accept LED and button actions
      SocketService.setCommand(action);
      // If it's a button press, update in-memory button state and emit to clients
      if (/^button[1-4]_press$/.test(action)) {
        const btnIdx = parseInt(action.match(/^button([1-4])_press$/)[1], 10);
        // Set only the pressed button to true, others to false
        const newButtonStates = { button1: false, button2: false, button3: false, button4: false };
        newButtonStates[`button${btnIdx}`] = true;
        // If web app controls are used (LED actions), always set button4 to 0
        if (/^led[1-3]_(on|off)$/.test(action)) {
          newButtonStates.button4 = false;
          // Broadcast updated button states to ESP32
          if (SocketService.io) SocketService.io.emit('buttonStates', newButtonStates);
        }
        SocketService.setButtonStates(newButtonStates);
        // Emit to all clients
        SocketService.emitButtonStates();
      }
      // Notify connected frontends immediately via WebSocket
      if (SocketService.io) SocketService.io.emit('commandUpdate', { action });

      // Respond immediately
      res.json({ message: 'Command set' });
    } catch (error) {
      console.error('Error setting command:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  }

  // GET /api/button-states - Return current button states for frontend and ESP32
  static async getButtonStates(req, res) {
    try {
      const states = SocketService.getButtonStates();
      res.json({ states });
    } catch (error) {
      console.error('Error getting button states:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  }

  static async getCommand(req, res) {
    try {
      // Do NOT clear the command here; just return it
      const cmd = SocketService.currentCommand;
      //console.log('[DEBUG] /api/command polled. currentCommand =', cmd);
      res.json({ command: cmd || null });
    } catch (error) {
      console.error('Error getting command:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  }

  static async postAck(req, res) {
    try {
      const { led, state } = req.body;
      console.log('[DEBUG] /api/ack called. Body:', req.body, 'currentCommand:', SocketService.currentCommand);
      if (!led || typeof state === 'undefined') {
        return res.status(400).json({ error: 'led and state required' });
      }

      if (SocketService.ledStates && Object.prototype.hasOwnProperty.call(SocketService.ledStates, led)) {
        const newState = !!state;
        // Only update and persist if state actually changed
        if (SocketService.ledStates[led] !== newState) {
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
        }
        // Clear the command ONLY after ACK from ESP32
        const cleared = SocketService.getAndClearCommand();
        console.log('[DEBUG] /api/ack: Cleared command:', cleared);
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
