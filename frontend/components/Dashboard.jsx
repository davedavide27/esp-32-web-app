import React, { useState, useEffect, useRef } from 'react';
import io from 'socket.io-client';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer, BarChart, Bar, AreaChart, Area } from 'recharts';
import { Thermometer, Droplets, Activity, Wifi, WifiOff, RefreshCw, Trash2, BarChart3, Clock, Database, Zap } from 'lucide-react';
import { saveAs } from 'file-saver';
import html2canvas from 'html2canvas';
import '../src/styles/Dashboard.css';

// Auto-detect backend server URL
const getBackendUrl = () => {
  // If in production, use the current hostname
  if (window.location.hostname !== 'localhost' && window.location.hostname !== '127.0.0.1') {
    return `http://${window.location.hostname}:5000`;
  }
  // For localhost development, use localhost
  return 'http://localhost:5000';
};

const BACKEND_URL = getBackendUrl();

const Dashboard = () => {
  const [realTimeData, setRealTimeData] = useState([]);
  const [savedData, setSavedData] = useState([]);
  const [esp32Status, setEsp32Status] = useState('inactive');
  const [socket, setSocket] = useState(null);
  const [lastUpdate, setLastUpdate] = useState(null);
  const [isLoading, setIsLoading] = useState(true);
  const [chartType, setChartType] = useState('line');
  const [dateRange, setDateRange] = useState({ from: '', to: '' });
  const [theme, setTheme] = useState('light');
  const chartRef = useRef(null);
  const statusTimeoutRef = useRef(null);

  // Notification logic (minimal, no animation)
  const showNotification = (message, type = 'info') => {
    const notification = document.createElement('div');
    notification.className = `notification notification-${type}`;
    notification.textContent = message;
    document.body.appendChild(notification);
    setTimeout(() => notification.remove(), 2000);
  };

  // Fetch historical data
  const fetchHistoricalData = async (from, to) => {
    try {
      let url = `${BACKEND_URL}/api/sensor-data`;
      if (from && to) {
        url += `?from=${from}&to=${to}`;
      }
      const response = await fetch(url);
      const data = await response.json();
      setSavedData(data.slice(-50));
      setIsLoading(false);
    } catch (error) {
      console.error('Error fetching historical data:', error);
      setIsLoading(false);
    }
  };

  // Fetch last update timestamp
  const fetchLastUpdate = async () => {
    try {
      const response = await fetch(`${BACKEND_URL}/api/last-update`);
      const data = await response.json();
      if (data.lastUpdate) {
        setLastUpdate(new Date(data.lastUpdate));
      }
    } catch (error) {
      console.error('Error fetching last update:', error);
    }
  };

  const clearData = () => {
    setRealTimeData([]);
    setLastUpdate(null);
  };

  const refreshData = () => {
    fetchHistoricalData();
  };

  const formatDataForChart = (data) => {
    return data.map((item) => ({
      time: new Date(item.timestamp).toLocaleTimeString(),
      temperature: parseFloat(item.temperature),
      humidity1: parseFloat(item.humidity1),
      temperature2: parseFloat(item.temperature2),
      humidity2: parseFloat(item.humidity2),
      current: parseFloat(item.current),
      timestamp: item.timestamp
    }));
  };

  const getCurrentValues = () => {
    if (realTimeData.length === 0) return { temperature: 'N/A', humidity1: 'N/A', temperature2: 'N/A', humidity2: 'N/A', current: 'N/A' };
    const latest = realTimeData[realTimeData.length - 1];
    return {
      temperature: `${latest.temperature}°C`,
      humidity1: `${latest.humidity1}%`,
      temperature2: `${latest.temperature2}°C`,
      humidity2: `${latest.humidity2}%`,
      current: `${latest.current}A`
    };
  };

  const handleChartTypeChange = (type) => {
    setChartType(type);
  };

  const handleDateChange = (e) => {
    const { name, value } = e.target;
    setDateRange(prev => ({ ...prev, [name]: value }));
  };

  const applyDateFilter = () => {
    if (dateRange.from && dateRange.to) {
      fetchHistoricalData(dateRange.from, dateRange.to);
    }
  };

  const exportCSV = () => {
    if (!realTimeData.length) return;
    const header = 'Time,Temperature (°C),Humidity 1 (%),Temp 2 (°C),Humidity 2 (%),Current (A)\n';
    const rows = realTimeData.map(d => `${new Date(d.timestamp).toLocaleString()},${d.temperature},${d.humidity1},${d.temperature2},${d.humidity2},${d.current}`).join('\n');
    const blob = new Blob([header + rows], { type: 'text/csv;charset=utf-8;' });
    saveAs(blob, 'sensor_data.csv');
  };

  const exportPNG = async () => {
    if (!chartRef.current) return;
    const canvas = await html2canvas(chartRef.current);
    canvas.toBlob(blob => {
      saveAs(blob, 'sensor_chart.png');
    });
  };

  const toggleTheme = () => {
    const newTheme = theme === 'light' ? 'dark' : 'light';
    setTheme(newTheme);
    const root = document.documentElement;
    root.classList.add('theme-transition');
    root.classList.toggle('dark', newTheme === 'dark');
    setTimeout(() => {
      root.classList.remove('theme-transition');
    }, 600);
  };

  useEffect(() => {
    if (esp32Status === 'active') {
      showNotification('ESP32 Connected', 'success');
    } else if (esp32Status === 'inactive') {
      showNotification('ESP32 Disconnected', 'error');
    }
  }, [esp32Status]);

  useEffect(() => {
    if (lastUpdate) {
      showNotification('Sensor data updated', 'info');
    }
  }, [lastUpdate]);

  useEffect(() => {
    const newSocket = io(BACKEND_URL, {
      transports: ['websocket', 'polling'],
      reconnection: true,
      reconnectionAttempts: 5,
      reconnectionDelay: 1000
    });
    setSocket(newSocket);

    newSocket.on('connect', () => {
      console.log('Connected to server');
    });

    newSocket.on('disconnect', () => {
      console.log('Disconnected from server');
    });

    newSocket.on('connect_error', (error) => {
      console.error('Connection error:', error);
    });

    newSocket.on('sensorData', (data) => {
      setRealTimeData(prevData => [...prevData.slice(-49), data]);
      setIsLoading(false);
      setEsp32Status('active');

      // Clear any existing timeout
      if (statusTimeoutRef.current) {
        clearTimeout(statusTimeoutRef.current);
      }

      // Set new timeout to reset to inactive after 30 seconds of no data
      statusTimeoutRef.current = setTimeout(() => {
        setEsp32Status('inactive');
      }, 30000);
    });

    newSocket.on('savedSensorData', (data) => {
      setSavedData(prevData => [...prevData.slice(-49), data]);
      setLastUpdate(new Date());
    });

    fetchHistoricalData();
    fetchLastUpdate();

    // Prevent page refresh on accidental F5 or browser refresh
    const handleBeforeUnload = (e) => {
      e.preventDefault();
      e.returnValue = 'Are you sure you want to leave? Live sensor data will be lost.';
    };

    window.addEventListener('beforeunload', handleBeforeUnload);

    return () => {
      window.removeEventListener('beforeunload', handleBeforeUnload);
      newSocket.close();
    };
  }, []);

  const current = getCurrentValues();

  return (
    <div className={`dashboard-root ${theme === 'dark' ? 'dark' : ''}`}> 
      <div className="dashboard-container">
        {/* Header */}
        <header className="dashboard-header">
          <div className="dashboard-title-group">
            <Activity className="dashboard-title-icon" />
            <h1 className="dashboard-title">ESP32 Sensor Dashboard</h1>
          </div>
          {/* Theme Switch Toggle */}
          <label className="theme-switch large">
            <input
              type="checkbox"
              checked={theme === 'dark'}
              onChange={toggleTheme}
              aria-label="Toggle dark mode"
            />
            <span className="slider">
              <span className="icon sun">☀️</span>
              <span className="icon moon">🌙</span>
            </span>
          </label>
        </header>

        {/* Status Bar */}
        <section className="dashboard-status-bar">
          <div className={`dashboard-status ${esp32Status === 'active' ? 'status-online' : 'status-offline'}`}>
            {esp32Status === 'active' ? <><Wifi className="status-icon" />ESP32 Connected</> : <><WifiOff className="status-icon" />ESP32 Disconnected</>}
          </div>
          <div className="dashboard-status-info">
            <Clock className="status-icon" />
            <span>Last Update:</span>
            <span>{lastUpdate ? lastUpdate.toLocaleString() : 'Never'}</span>
          </div>
          <div className="dashboard-status-info">
            <Database className="status-icon" />
            <span>Data Points:</span>
            <span>{savedData.length}</span>
          </div>
        </section>

        {/* Current Values */}
        <section className="dashboard-current">
          <div className="dashboard-current-card">
            <Thermometer className="current-icon temp" />
            <div>
              <div className="current-label">Temperature</div>
              <div className="current-value temp">{current.temperature}</div>
            </div>
          </div>
          <div className="dashboard-current-card">
            <Droplets className="current-icon hum" />
            <div>
              <div className="current-label">Humidity 1</div>
              <div className="current-value hum">{current.humidity1}</div>
            </div>
          </div>
          <div className="dashboard-current-card">
            <Thermometer className="current-icon temp" />
            <div>
              <div className="current-label">Temperature 2</div>
              <div className="current-value temp">{current.temperature2}</div>
            </div>
          </div>
          <div className="dashboard-current-card">
            <Droplets className="current-icon hum" />
            <div>
              <div className="current-label">Humidity 2</div>
              <div className="current-value hum">{current.humidity2}</div>
            </div>
          </div>
          <div className="dashboard-current-card">
            <Zap className="current-icon current" />
            <div>
              <div className="current-label">Current</div>
              <div className="current-value current">{current.current}</div>
            </div>
          </div>
        </section>

        {/* Chart Section */}
        <section className="dashboard-chart-section">
          <div className="dashboard-chart-header">
            <h2 className="dashboard-chart-title"><BarChart3 className="chart-title-icon" />Data Visualization</h2>
            <div className="dashboard-chart-controls">
              <button onClick={() => handleChartTypeChange('line')} className={`chart-type-btn${chartType==='line' ? ' active' : ''}`}>Line</button>
              <button onClick={() => handleChartTypeChange('bar')} className={`chart-type-btn${chartType==='bar' ? ' active' : ''}`}>Bar</button>
              <button onClick={() => handleChartTypeChange('area')} className={`chart-type-btn${chartType==='area' ? ' active' : ''}`}>Area</button>
            </div>
            <div className="dashboard-chart-exports">
              <input type="date" name="from" value={dateRange.from} onChange={handleDateChange} className="chart-date-input" />
              <span className="chart-date-sep">to</span>
              <input type="date" name="to" value={dateRange.to} onChange={handleDateChange} className="chart-date-input" />
              <button onClick={applyDateFilter} className="chart-action-btn">Apply</button>
              <button onClick={exportCSV} className="chart-action-btn">Export CSV</button>
              <button onClick={exportPNG} className="chart-action-btn">Export PNG</button>
              <button onClick={refreshData} className="chart-action-btn"><RefreshCw className="chart-action-icon" />Refresh</button>
              <button onClick={clearData} className="chart-action-btn danger"><Trash2 className="chart-action-icon" />Clear</button>
            </div>
          </div>
          <div ref={chartRef} className="dashboard-chart-area">
            {isLoading ? (
              <div className="dashboard-loading">Loading...</div>
            ) : savedData.length === 0 ? (
              <div className="dashboard-nodata">No data available. Waiting for ESP32 connection...</div>
            ) : (
              <ResponsiveContainer width="100%" height={320}>
                {chartType === 'line' && (
                  <LineChart data={formatDataForChart(savedData)}>
                    <CartesianGrid strokeDasharray="3 3" stroke="#e0e7ff" />
                    <XAxis dataKey="time" stroke="#6b7280" fontSize={10} tick={{ fill: '#6b7280' }} />
                    <YAxis stroke="#6b7280" fontSize={10} tick={{ fill: '#6b7280' }} />
                    <Tooltip contentStyle={{ backgroundColor: theme === 'dark' ? '#23272f' : '#fff', color: theme === 'dark' ? '#f3f4f6' : '#222', border: "1px solid #e2e8f0", borderRadius: '8px' }} />
                    <Legend />
                    <Line type="monotone" dataKey="temperature" stroke="#3b82f6" strokeWidth={2} dot={false} name="Temperature (°C)" isAnimationActive={true} animationDuration={900} />
                    <Line type="monotone" dataKey="humidity1" stroke="#10b981" strokeWidth={2} dot={false} name="Humidity 1 (%)" isAnimationActive={true} animationDuration={900} />
                    <Line type="monotone" dataKey="temperature2" stroke="#f59e0b" strokeWidth={2} dot={false} name="Temp 2 (°C)" isAnimationActive={true} animationDuration={900} />
                    <Line type="monotone" dataKey="humidity2" stroke="#ef4444" strokeWidth={2} dot={false} name="Humidity 2 (%)" isAnimationActive={true} animationDuration={900} />
                    <Line type="monotone" dataKey="current" stroke="#8b5cf6" strokeWidth={2} dot={false} name="Current (A)" isAnimationActive={true} animationDuration={900} />
                  </LineChart>
                )}
                {chartType === 'bar' && (
                  <BarChart data={formatDataForChart(savedData)}>
                    <CartesianGrid strokeDasharray="3 3" stroke="#e0e7ff" />
                    <XAxis dataKey="time" stroke="#6b7280" fontSize={10} tick={{ fill: '#6b7280' }} />
                    <YAxis stroke="#6b7280" fontSize={10} tick={{ fill: '#6b7280' }} />
                    <Tooltip contentStyle={{ backgroundColor: theme === 'dark' ? '#23272f' : '#fff', color: theme === 'dark' ? '#f3f4f6' : '#222', border: "1px solid #e2e8f0", borderRadius: '8px' }} />
                    <Legend />
                    <Bar dataKey="temperature" fill="#3b82f6" name="Temperature (°C)" radius={[2, 2, 0, 0]} isAnimationActive={true} animationDuration={900} />
                    <Bar dataKey="humidity1" fill="#10b981" name="Humidity 1 (%)" radius={[2, 2, 0, 0]} isAnimationActive={true} animationDuration={900} />
                    <Bar dataKey="temperature2" fill="#f59e0b" name="Temp 2 (°C)" radius={[2, 2, 0, 0]} isAnimationActive={true} animationDuration={900} />
                    <Bar dataKey="humidity2" fill="#ef4444" name="Humidity 2 (%)" radius={[2, 2, 0, 0]} isAnimationActive={true} animationDuration={900} />
                    <Bar dataKey="current" fill="#8b5cf6" name="Current (A)" radius={[2, 2, 0, 0]} isAnimationActive={true} animationDuration={900} />
                  </BarChart>
                )}
                {chartType === 'area' && (
                  <AreaChart data={formatDataForChart(savedData)}>
                    <defs>
                      <linearGradient id="tempArea" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="5%" stopColor="#3b82f6" stopOpacity={0.3}/>
                        <stop offset="95%" stopColor="#3b82f6" stopOpacity={0}/>
                      </linearGradient>
                      <linearGradient id="voltArea" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="5%" stopColor="#10b981" stopOpacity={0.3}/>
                        <stop offset="95%" stopColor="#10b981" stopOpacity={0}/>
                      </linearGradient>
                    </defs>
                    <CartesianGrid strokeDasharray="3 3" stroke="#e0e7ff" />
                    <XAxis dataKey="time" stroke="#6b7280" fontSize={10} tick={{ fill: '#6b7280' }} />
                    <YAxis stroke="#6b7280" fontSize={10} tick={{ fill: '#6b7280' }} />
                    <Tooltip contentStyle={{ backgroundColor: theme === 'dark' ? '#23272f' : '#fff', color: theme === 'dark' ? '#f3f4f6' : '#222', border: "1px solid #e2e8f0", borderRadius: '8px' }} />
                    <Legend />
                    <Area type="monotone" dataKey="temperature" stroke="#3b82f6" fill="url(#tempArea)" fillOpacity={1} name="Temperature (°C)" isAnimationActive={true} animationDuration={900} />
                    <Area type="monotone" dataKey="humidity1" stroke="#10b981" fill="url(#voltArea)" fillOpacity={1} name="Humidity 1 (%)" isAnimationActive={true} animationDuration={900} />
                    <Area type="monotone" dataKey="temperature2" stroke="#f59e0b" fill="url(#tempArea)" fillOpacity={1} name="Temp 2 (°C)" isAnimationActive={true} animationDuration={900} />
                    <Area type="monotone" dataKey="humidity2" stroke="#ef4444" fill="url(#voltArea)" fillOpacity={1} name="Humidity 2 (%)" isAnimationActive={true} animationDuration={900} />
                    <Area type="monotone" dataKey="current" stroke="#8b5cf6" fill="url(#tempArea)" fillOpacity={1} name="Current (A)" isAnimationActive={true} animationDuration={900} />
                  </AreaChart>
                )}
              </ResponsiveContainer>
            )}
          </div>
        </section>

        {/* Recent Data Table */}
        <section className="dashboard-table-section">
          <h3 className="dashboard-table-title"><Database className="table-title-icon" />Recent Sensor Data</h3>
          <div className="dashboard-table-wrapper">
            <table className="dashboard-table">
              <thead>
                <tr>
                  <th>#</th>
                  <th>Time</th>
                  <th>Date</th>
                  <th>Temperature (°C)</th>
                  <th>Humidity 1 (%)</th>
                  <th>Temp 2 (°C)</th>
                  <th>Humidity 2 (%)</th>
                  <th>Current (A)</th>
                </tr>
              </thead>
              <tbody>
                {savedData.slice(-10).reverse().map((data, idx) => (
                  <tr key={data.id || idx}>
                    <td>{savedData.length - idx}</td>
                    <td>{new Date(data.timestamp).toLocaleTimeString()}</td>
                    <td>{new Date(data.timestamp).toLocaleDateString()}</td>
                    <td>{data.temperature}</td>
                    <td>{data.humidity1}</td>
                    <td>{data.temperature2}</td>
                    <td>{data.humidity2}</td>
                    <td>{data.current}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </section>
      </div>
    </div>
  );
};

export default Dashboard;
