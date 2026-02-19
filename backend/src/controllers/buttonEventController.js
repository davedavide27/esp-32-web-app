// Controller for button_events
const ButtonEvent = require('../models/buttonEvent');

// POST /api/button-event
const logButtonEvent = async (req, res) => {
  const { button_number, source, state } = req.body;
  if (![1,2,3,4].includes(button_number) || !['manual','web'].includes(source)) {
    return res.status(400).json({ error: 'Invalid button event data' });
  }
  try {
    await ButtonEvent.createButtonEvent({ button_number, source, state });
    res.status(201).json({ success: true });
  } catch (err) {
    res.status(500).json({ error: 'Failed to log button event' });
  }
};

// GET /api/button-events
const getButtonEvents = async (req, res) => {
  try {
    const events = await ButtonEvent.getButtonEvents();
    res.json(events);
  } catch (err) {
    res.status(500).json({ error: 'Failed to fetch button events' });
  }
};

module.exports = {
  logButtonEvent,
  getButtonEvents
};
