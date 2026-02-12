exports.up = function(knex) {
  return knex.schema.createTable('led_states', function(table) {
    table.string('led', 16).primary(); // e.g., 'led1', 'led2', 'led3'
    table.boolean('state').defaultTo(false); // true = on, false = off
    table.timestamp('updated_at').defaultTo(knex.fn.now());
  });
};

exports.down = function(knex) {
  return knex.schema.dropTable('led_states');
};
