/**
 * @param { import("knex").Knex } knex
 * @returns { Promise<void> }
 */
exports.up = function(knex) {
  return knex.schema.alterTable('sensor_data', function(table) {
    table.integer('motion').defaultTo(0);
    table.float('temperature1');
  });
};

/**
 * @param { import("knex").Knex } knex
 * @returns { Promise<void> }
 */
exports.down = function(knex) {
  return knex.schema.alterTable('sensor_data', function(table) {
    table.dropColumn('motion');
    table.dropColumn('temperature1');
  });
};
