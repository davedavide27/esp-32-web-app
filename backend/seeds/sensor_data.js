exports.seed = function(knex) {
  return knex('sensor_data').del()
    .then(function () {
      return knex('sensor_data').insert([
        { temperature: 25.0, voltage: 3.3 },
        { temperature: 26.5, voltage: 3.2 },
        { temperature: 24.8, voltage: 3.4 },
      ]);
    });
};
