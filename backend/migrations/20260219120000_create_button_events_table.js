// Migration: Create button_events table
exports.up = function(knex) {
  return knex.schema.createTable('button_events', function(table) {
    table.increments('id').primary();
    table.integer('button_number').notNullable(); // 1-4
    table.string('source').notNullable(); // 'manual' or 'web'
    table.boolean('state').notNullable(); // true=pressed, false=released
    table.timestamp('created_at').defaultTo(knex.fn.now());
  });
};

exports.down = function(knex) {
  return knex.schema.dropTable('button_events');
};
