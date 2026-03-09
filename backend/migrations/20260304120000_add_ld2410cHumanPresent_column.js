exports.up = function(knex) {
  return knex.schema.alterTable('sensor_data', function(table) {
    table.boolean('ld2410cHumanPresent').defaultTo(false);
  });
};

exports.down = function(knex) {
  return knex.schema.alterTable('sensor_data', function(table) {
    table.dropColumn('ld2410cHumanPresent');
  });
};
