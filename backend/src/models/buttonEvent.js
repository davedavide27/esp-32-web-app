// Model for button_events table
const knex = require('../../config/database');

const createButtonEvent = async ({ button_number, source, state }) => {
  return knex('button_events').insert({
    button_number,
    source,
    state
  });
};

const getButtonEvents = async () => {
  return knex('button_events').select('*').orderBy('created_at', 'desc');
};

module.exports = {
  createButtonEvent,
  getButtonEvents
};
