exports.up = function(knex) {
  return knex.schema.table('sensor_data', function(table) {
    table.boolean('fanOn').defaultTo(false);
  });
};

exports.down = function(knex) {
  return knex.schema.table('sensor_data', function(table) {
    table.dropColumn('fanOn');
  });
};
