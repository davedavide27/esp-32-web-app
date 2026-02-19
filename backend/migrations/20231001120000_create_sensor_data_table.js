exports.up = function(knex) {
  return knex.schema.createTable('sensor_data', function(table) {
    table.increments('id').primary();
    table.float('temperature');
    table.float('voltage');
    table.boolean('button1').defaultTo(false);
    table.boolean('button2').defaultTo(false);
    table.boolean('button3').defaultTo(false);
    table.boolean('button4').defaultTo(false);
    table.timestamp('timestamp').defaultTo(knex.fn.now());
  });
};

exports.down = function(knex) {
  return knex.schema.dropTable('sensor_data');
};
