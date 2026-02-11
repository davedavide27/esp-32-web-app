# ESP32 Monitor

A web application to monitor data from ESP32 microcontroller in real-time.

## Tech Stack

- **Backend:** Node.js, Express.js, Socket.io, MySQL
- **Frontend:** React, TailwindCSS, Recharts

## Project Structure

```
esp32-monitor/
├── backend/
│   ├── src/
│   │   ├── controllers/
│   │   ├── models/
│   │   ├── routes/
│   │   └── services/
│   ├── tests/
│   ├── config/
│   ├── package.json
│   └── server.js
├── frontend/
│   ├── src/
│   ├── components/
│   ├── pages/
│   ├── public/
│   └── package.json
├── database/
│   └── schema.sql
├── docker/
│   └── docker-compose.yml
├── README.md
└── .gitignore
```

## Setup

### Option 1: Using Docker (Recommended)

1. **Start MySQL Database:**
   ```bash
   cd docker
   docker-compose up -d
   ```

2. **Backend Setup:**
   ```bash
   cd backend
   npm install
   # Run database migrations
   npx knex migrate:latest
   # (Optional) Run seeds to populate initial data
   npx knex seed:run
   npm run dev
   ```

3. **Frontend Setup:**
   ```bash
   cd frontend
   npm install
   npm start
   ```

### Option 2: Local MySQL Setup

1. **Install MySQL locally:**
   - Download and install MySQL from https://dev.mysql.com/downloads/mysql/
   - Or use package managers like `brew install mysql` (macOS) or `sudo apt install mysql-server` (Ubuntu)

2. **Create Database:**
   ```sql
   CREATE DATABASE esp32_monitor;
   ```

3. **Configure Environment:**
   ```bash
   cd backend
   cp .env.example .env
   # Edit .env with your local MySQL credentials
   ```

4. **Backend Setup:**
   ```bash
   cd backend
   npm install
   # Run database migrations
   npx knex migrate:latest
   # (Optional) Run seeds to populate initial data
   npx knex seed:run
   npm run dev
   ```

5. **Frontend Setup:**
   ```bash
   cd frontend
   npm install
   npm start
   ```

## ESP32 Integration

The ESP32 should send JSON data via WebSocket to the backend server. Example data format:
```json
{
  "temperature": 25.5,
  "voltage": 3.3
}
```

## Deployment

Use Docker for deployment. Build and run the containers as needed.
