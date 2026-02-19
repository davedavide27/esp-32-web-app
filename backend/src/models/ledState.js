// Model for led_states table
const knex = require('../../config/database');

const setLedState = async (led, state) => {
  return knex('led_states').insert({
    led,
    state,
    updated_at: knex.fn.now()
  }).onConflict('led').merge();
};

const getLedStates = async () => {
  return knex('led_states').select('*');
};

module.exports = {
  setLedState,
  getLedStates
};
