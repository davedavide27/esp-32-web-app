// Custom Pagination Component (converted from provided JS)
function Pagination({ pages, page, onPageChange }) {
  let pageCutLow = page - 1;
  let pageCutHigh = page + 1;
  const items = [];
  // Previous button (always visible, disabled if first page)
  items.push(
    <li className={"page-item previous no" + (page === 1 ? " disabled" : "") } key="prev">
      <a
        onClick={page === 1 ? undefined : () => onPageChange(page - 1)}
        style={page === 1 ? { background: '#e5e7eb', color: '#9ca3af', cursor: 'not-allowed', pointerEvents: 'none' } : {}}
        aria-disabled={page === 1}
      >Previous</a>
    </li>
  );
  if (pages < 6) {
    for (let p = 1; p <= pages; p++) {
      items.push(
        <li className={page === p ? "active" : "no"} key={p}>
          <a onClick={() => onPageChange(p)}>{p}</a>
        </li>
      );
    }
  } else {
    if (page > 2) {
      items.push(
        <li className="no page-item" key={1}>
          <a onClick={() => onPageChange(1)}>1</a>
        </li>
      );
      if (page > 3) {
        items.push(
          <li className="out-of-range" key="start-ellipsis">
            <a onClick={() => onPageChange(page - 2)}>...</a>
          </li>
        );
      }
    }
    if (page === 1) {
      pageCutHigh += 2;
    } else if (page === 2) {
      pageCutHigh += 1;
    }
    if (page === pages) {
      pageCutLow -= 2;
    } else if (page === pages - 1) {
      pageCutLow -= 1;
    }
    for (let p = pageCutLow; p <= pageCutHigh; p++) {
      if (p === 0) p = 1;
      if (p > pages) continue;
      items.push(
        <li className={"page-item " + (page === p ? "active" : "no")} key={p}>
          <a onClick={() => onPageChange(p)}>{p}</a>
        </li>
      );
    }
    if (page < pages - 1) {
      if (page < pages - 2) {
        items.push(
          <li className="out-of-range" key="end-ellipsis">
            <a onClick={() => onPageChange(page + 2)}>...</a>
          </li>
        );
      }
      items.push(
        <li className="page-item no" key={pages}>
          <a onClick={() => onPageChange(pages)}>{pages}</a>
        </li>
      );
    }
  }
  // Next button (always visible, disabled if last page)
  items.push(
    <li className={"page-item next no" + (page === pages ? " disabled" : "") } key="next">
      <a
        onClick={page === pages ? undefined : () => onPageChange(page + 1)}
        style={page === pages ? { background: '#e5e7eb', color: '#9ca3af', cursor: 'not-allowed', pointerEvents: 'none' } : {}}
        aria-disabled={page === pages}
      >Next</a>
    </li>
  );
  return (
    <div id="pagination">
      <ul>{items}</ul>
    </div>
  );
}
import React, { useState, useEffect, useRef } from 'react';
import io from 'socket.io-client';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer, BarChart, Bar, AreaChart, Area } from 'recharts';
import { Thermometer, Droplets, Activity, Wifi, WifiOff, RefreshCw, Trash2, BarChart3, Clock, Database, Zap } from 'lucide-react';
import { saveAs } from 'file-saver';
import html2canvas from 'html2canvas';
import '../src/styles/Dashboard.css';
import { AnimatePresence, motion } from 'framer-motion';

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

  // Send a command to the ESP32 via HTTP POST (not WebSocket)
  const sendLedCommand = async (action) => {
    try {
      const resp = await fetch(`${BACKEND_URL}/api/command`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action })
      });
      if (resp.ok) {
        showNotification(`Sent command: ${action}`, 'info');
      } else {
        showNotification('Failed to send command', 'error');
      }
    } catch (err) {
      showNotification('Failed to send command', 'error');
      console.error('Error sending command:', err);
    }
  };

  // Helper to send an LED toggle for a specific LED index (1-3) via WebSocket, with instant optimistic update
  const sendLedToggle = (ledIndex, turnOn) => {
    const action = `led${ledIndex}_${turnOn ? 'on' : 'off'}`;
    const key = `led${ledIndex}`;
    // Optimistically update UI immediately
    setLedPending(prev => ({ ...prev, [key]: true }));
    setLedStates(prev => ({ ...prev, [key]: turnOn }));
    sendLedCommand(action);
  };

  const [controlsOpen, setControlsOpen] = useState(false);
  const [ledStates, setLedStates] = useState({ led1: false, led2: false, led3: false });
  const [ledPending, setLedPending] = useState({ led1: false, led2: false, led3: false });

  const clearData = () => {
    setRealTimeData([]);
    setLastUpdate(null);
  };

  const refreshData = () => {
    fetchHistoricalData();
  };

  const formatDataForChart = (data) => {
    return data.map((item) => {
      const radarValue = item.ld2410cHumanPresent == null ? 0 : parseInt(item.ld2410cHumanPresent);
      return {
        time: item.timestamp ? new Date(item.timestamp).toLocaleTimeString() : 'N/A',
        temperature: item.temperature == null || isNaN(item.temperature) ? 0 : parseFloat(item.temperature),
        humidity1: item.humidity1 == null || isNaN(item.humidity1) ? 0 : parseFloat(item.humidity1),
        voltage: item.voltage == null || isNaN(item.voltage) ? 0 : parseFloat(item.voltage),
        button1: item.button1,
        button2: item.button2,
        button3: item.button3,
        button4: item.button4,
        ld2410cHumanPresent: radarValue,
        radar: radarValue === 1 ? 1 : 0,
        radarLabel: radarValue === 1 ? 'Human Present' : 'None',
        timestamp: item.timestamp
      };
    });
  };

  const getCurrentValues = () => {
    if (realTimeData.length === 0) return {
      temperature: 'N/A',
      humidity1: 'N/A',
      voltage: 'N/A',
      fanOn: 'N/A',
      button1: 'N/A',
      button2: 'N/A',
      button3: 'N/A',
      button4: 'N/A',
      ld2410cHumanPresent: 'N/A'
    };
    const latest = realTimeData[realTimeData.length - 1];
    return {
      temperature: `${latest.temperature == null || isNaN(latest.temperature) ? 0 : latest.temperature}\u00b0C`,
      humidity1: `${latest.humidity1 == null || isNaN(latest.humidity1) ? 0 : latest.humidity1}%`,
      voltage: `${latest.voltage == null || isNaN(latest.voltage) ? 0 : latest.voltage}V`,
      ld2410cHumanPresent: latest.ld2410cHumanPresent === 1 || latest.ld2410cHumanPresent === true || latest.ld2410cHumanPresent === 'true' ? 'Human Present' : 'N/A',
      fanOn: latest.fanOn ? 'ON' : 'OFF',
      button1: latest.button1 ? 'ON' : 'OFF',
      button2: latest.button2 ? 'ON' : 'OFF',
      button3: latest.button3 ? 'ON' : 'OFF',
      button4: latest.button4 ? 'ON' : 'OFF'
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
    
    // Listen for command updates (optional)
    newSocket.on('commandUpdate', (payload) => {
      if (payload && payload.action) {
        showNotification(`Command set: ${payload.action}`, 'info');
      }
    });

    // Listen for ledStates updates
    newSocket.on('ledStates', (states) => {
      if (states) {
        setLedStates(states);
        // clear pending flags for leds present in the update
        setLedPending(prev => {
          const next = { ...prev };
          Object.keys(states).forEach(k => { next[k] = false; });
          return next;
        });
      }
    });

    // Fetch initial LED states
    (async () => {
      try {
        const resp = await fetch(`${BACKEND_URL}/api/led-states`);
        const json = await resp.json();
        if (json && json.states) setLedStates(json.states);
      } catch (err) {
        console.error('Failed to fetch LED states', err);
      }
    })();

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

  const PAGE_SIZE = 10;
  const [currentPage, setCurrentPage] = useState(1);
  const paginatedData = savedData.slice().sort((a, b) => new Date(b.timestamp) - new Date(a.timestamp));
  const totalPages = Math.max(1, Math.ceil(paginatedData.length / PAGE_SIZE));
  const currentPageData = paginatedData.slice((currentPage - 1) * PAGE_SIZE, currentPage * PAGE_SIZE);
  const handlePageChange = (page) => {
    setCurrentPage(page);
  };

  const currentValues = getCurrentValues();

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
          <div className="dashboard-status-info">
            <span>Controls:</span>
            <button className="chart-action-btn" onClick={() => setControlsOpen(true)}>Open Controls</button>
          </div>
          <div className="dashboard-status-info">
          </div>
        </section>

        {/* Controls Modal */}
        {controlsOpen && (
          <div className="controls-modal" style={{position: 'fixed', top:0,left:0,right:0,bottom:0,background:'rgba(0,0,0,0.5)',display:'flex',alignItems:'center',justifyContent:'center'}}>
            <div className="controls-modal-content" style={{background:'#fff',padding:20,borderRadius:8,width:320}}>
              <h3>LED Controls</h3>
              <div style={{display:'flex',flexDirection:'column',gap:8}}>
                {[1,2,3].map(i => {
                  const key = `led${i}`;
                  const isOn = !!ledStates[key];
                  const pending = !!ledPending[key];
                  // Color and status text
                  const color = isOn ? (i === 1 ? '#22c55e' : i === 2 ? '#eab308' : '#ef4444') : '#6b7280';
                  const bg = isOn ? (i === 1 ? '#bbf7d0' : i === 2 ? '#fef9c3' : '#fecaca') : '#e5e7eb';
                  const status = isOn ? (i === 1 ? 'ON (Green)' : i === 2 ? 'ON (Yellow)' : 'ON (Red)') : 'OFF';
                  return (
                    <div key={i} style={{display:'flex',justifyContent:'space-between',alignItems:'center'}}>
                      <div style={{color, fontWeight:700}}>LED {i} <span style={{fontSize:'0.9em',color:'#6b7280'}}>({status})</span></div>
                      <div>
                        <button
                          className={`chart-action-btn ${isOn ? 'active' : ''}`}
                          style={{background:bg,color}}
                          onClick={() => sendLedToggle(i, !isOn)}
                          disabled={pending}
                        >
                          {pending ? 'Pending...' : (isOn ? 'Turn Off' : 'Turn On')}
                        </button>
                      </div>
                    </div>
                  );
                })}
              </div>
              <div style={{textAlign:'right',marginTop:12}}>
                <button className="chart-action-btn" onClick={() => setControlsOpen(false)}>Close</button>
              </div>
            </div>
          </div>
        )}

        {/* Current Values */}
        <section className="dashboard-current">
          <div className="dashboard-current-card">
            <Thermometer className="current-icon temp" />
            <div>
              <div className="current-label">Temperature</div>
              <div className="current-value temp">{currentValues.temperature}</div>
            </div>
          </div>
          <div className="dashboard-current-card">
            <Droplets className="current-icon hum" />
            <div>
              <div className="current-label">Humidity 1</div>
              <div className="current-value hum">{currentValues.humidity1}</div>
            </div>
          </div>
          <div className="dashboard-current-card">
            <span className="current-icon fan" role="img" aria-label="Fan">🌰</span>
            <div>
              <div className="current-label">Fan Knob</div>
              <div className="current-value fan">{currentValues.fanOn}</div>
            </div>
          </div>
          <div className="dashboard-current-card">
            <Zap className="current-icon voltage" />
            <div>
              <div className="current-label">Voltage</div>
              <div className="current-value voltage">{currentValues.voltage}</div>
            </div>
          </div>
          {/* LD2410C (Radar) as a separate card below Voltage */}
          <div className="dashboard-current-card">
            <span className="current-icon" role="img" aria-label="LD2410C">🛰️</span>
            <div>
              <div className="current-label">LD2410C (Radar)</div>
              <div className="current-value">{currentValues.ld2410cHumanPresent}</div>
            </div>
          </div>
          {/* LED State card remains after LD2410C */}
          <div className="dashboard-current-card">
            <span
              className="current-icon"
              style={{
                background: (() => {
                  // Priority: manual (button) control, then backend LED state
                  if (currentValues.button1 === 'ON' || ledStates.led1) return '#bbf7d0'; // green-200
                  if (currentValues.button2 === 'ON' || ledStates.led2) return '#fef9c3'; // yellow-100
                  if (currentValues.button3 === 'ON' || ledStates.led3) return '#fecaca'; // red-200
                  return '#e5e7eb'; // gray-200 for OFF
                })(),
                color: (() => {
                  if (currentValues.button1 === 'ON' || ledStates.led1) return '#22c55e'; // green-500
                  if (currentValues.button2 === 'ON' || ledStates.led2) return '#eab308'; // yellow-500
                  if (currentValues.button3 === 'ON' || ledStates.led3) return '#ef4444'; // red-500
                  return '#6b7280'; // gray-500 for OFF
                })(),
                fontWeight: 700,
                fontSize: '2rem',
                minWidth: '2.5rem',
                minHeight: '2.5rem',
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                borderRadius: '0.5rem',
                boxShadow: '0 1px 6px rgba(0,0,0,0.07)'
              }}
              title="LED State"
            >
              <svg width="1.5em" height="1.5em" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <circle cx="12" cy="12" r="8" />
                <text x="12" y="16" textAnchor="middle" fontSize="10" fill="currentColor" fontWeight="bold">
                  {(() => {
                    if (currentValues.button4 === 'ON') return 'OFF';
                    if (currentValues.button1 === 'ON') return 1;
                    if (currentValues.button2 === 'ON') return 2;
                    if (currentValues.button3 === 'ON') return 3;
                    // fallback to backend LED state if no manual control
                    const onIdx = [1,2,3].find(i => ledStates[`led${i}`]);
                    if (!onIdx) return 'OFF';
                    return onIdx;
                  })()}
                </text>
              </svg>
            </span>
            <div>
              <div className="current-label">LED State</div>
              <div className="current-value" style={{
                color: (() => {
                  const onIdx = [1,2,3].find(i => ledStates[`led${i}`]);
                  if (onIdx === 1) return '#22c55e';
                  if (onIdx === 2) return '#eab308';
                  if (onIdx === 3) return '#ef4444';
                  return '#6b7280';
                })(),
                fontWeight: 700
              }}>
                {(() => {
                  // Always show as "LED X ON" or "OFF" for consistency, using backend LED state as primary
                  const onIdx = [1,2,3].find(i => ledStates[`led${i}`]);
                  if (!onIdx) return 'OFF';
                  return `LED ${onIdx} ON`;
                })()}
              </div>
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
          {/* Chart Pagination Logic */}
          {(() => {
            const chartPageSize = 10;
            const chartTotalPages = Math.max(1, Math.ceil(savedData.length / chartPageSize));
            const [chartPage, setChartPage] = React.useState(1);
            const [animating, setAnimating] = React.useState(false);
            // Sort data from newest to oldest
            const chartFormattedData = formatDataForChart([...savedData].sort((a, b) => new Date(b.timestamp) - new Date(a.timestamp)));
            const chartPageData = chartFormattedData.slice((chartPage - 1) * chartPageSize, chartPage * chartPageSize);
            // Animation logic
            const handlePageChange = (newPage) => {
              if (newPage < 1 || newPage > chartTotalPages || newPage === chartPage) return;
              setAnimating(true);
              setTimeout(() => {
                setChartPage(newPage);
                setAnimating(false);
              }, 250);
            };
            return (
              <>
                <div style={{overflowX: 'auto', whiteSpace: 'nowrap'}}>
                  <div
                    ref={chartRef}
                    className="dashboard-chart-area"
                    style={{
                      minWidth: '320px',
                      width: '100%',
                      maxWidth: '100vw',
                      transition: animating ? 'opacity 0.25s' : undefined,
                      opacity: animating ? 0.5 : 1,
                      boxSizing: 'border-box',
                      background: theme === 'dark' ? '#18181b' : '#f9fafb',
                      borderRadius: 16,
                      boxShadow: theme === 'dark' ? '0 2px 8px #23272f' : '0 2px 8px #e0e7ff',
                      padding: '16px 8px',
                      overflowX: 'auto',
                      overflowY: 'visible',
                      border: 'none',
                    }}
                  >
                    {isLoading ? (
                      <div className="dashboard-loading">Loading...</div>
                    ) : savedData.length === 0 ? (
                      <div className="dashboard-nodata">No data available. Waiting for ESP32 connection...</div>
                    ) : (
                      <ResponsiveContainer width="100%" minWidth={320} height={320}>
                        {chartType === 'line' && (
                          <LineChart data={chartPageData}>
                            <CartesianGrid strokeDasharray="3 3" stroke="#e0e7ff" />
                            <XAxis dataKey="time" stroke="#6b7280" fontSize={10} tick={{ fill: '#6b7280' }} />
                            <YAxis stroke="#6b7280" fontSize={10} tick={{ fill: '#6b7280' }} />
                            <Tooltip
                              contentStyle={{ backgroundColor: theme === 'dark' ? '#23272f' : '#fff', color: theme === 'dark' ? '#f3f4f6' : '#222', border: "1px solid #e2e8f0", borderRadius: '8px' }}
                              formatter={(value, name, props) => {
                                if (name === 'Radar') {
                                  return [props.payload.radarLabel, 'Radar'];
                                }
                                return [value, name];
                              }}
                            />
                            <Legend />
                            <Line type="monotone" dataKey="temperature" stroke="#3b82f6" strokeWidth={2} dot={false} name="Temperature (°C)" isAnimationActive={true} animationDuration={900} />
                            <Line type="monotone" dataKey="humidity1" stroke="#10b981" strokeWidth={2} dot={false} name="Humidity 1 (%)" isAnimationActive={true} animationDuration={900} />
                            <Line
                              type="stepAfter"
                              dataKey="radar"
                              stroke="#e11d48" // More vivid color
                              strokeWidth={4}
                              strokeDasharray="6 3"
                              dot={{ r: 6, stroke: '#e11d48', strokeWidth: 2, fill: '#fff' }}
                              name="Radar"
                              legendType="diamond"
                              isAnimationActive={true}
                              animationDuration={900}
                            />
                            <Line type="monotone" dataKey="voltage" stroke="#22d3ee" strokeWidth={2} dot={false} name="Voltage (V)" isAnimationActive={true} animationDuration={900} />
                          </LineChart>
                        )}
                        {chartType === 'bar' && (
                          <BarChart data={chartPageData}>
                            <CartesianGrid strokeDasharray="3 3" stroke="#e0e7ff" />
                            <XAxis dataKey="time" stroke="#6b7280" fontSize={10} tick={{ fill: '#6b7280' }} />
                            <YAxis stroke="#6b7280" fontSize={10} tick={{ fill: '#6b7280' }} />
                            <Tooltip
                              contentStyle={{ backgroundColor: theme === 'dark' ? '#23272f' : '#fff', color: theme === 'dark' ? '#f3f4f6' : '#222', border: "1px solid #e2e8f0", borderRadius: '8px' }}
                              formatter={(value, name, props) => {
                                if (name === 'Radar') {
                                  return [props.payload.radarLabel, 'Radar'];
                                }
                                return [value, name];
                              }}
                            />
                            <Legend />
                            <Bar dataKey="temperature" fill="#3b82f6" name="Temperature (°C)" radius={[2, 2, 0, 0]} isAnimationActive={true} animationDuration={900} />
                            <Bar dataKey="humidity1" fill="#10b981" name="Humidity 1 (%)" radius={[2, 2, 0, 0]} isAnimationActive={true} animationDuration={900} />
                            <Bar
                              dataKey="radar"
                              fill="#e11d48"
                              name="Radar"
                              radius={[4, 4, 0, 0]}
                              legendType="diamond"
                              isAnimationActive={true}
                              animationDuration={900}
                            />
                            <Bar dataKey="voltage" fill="#22d3ee" name="Voltage (V)" radius={[2, 2, 0, 0]} isAnimationActive={true} animationDuration={900} />
                          </BarChart>
                        )}
                        {chartType === 'area' && (
                          <AreaChart data={chartPageData}>
                            <defs>
                              <linearGradient id="tempArea" x1="0" y1="0" x2="0" y2="1">
                                <stop offset="5%" stopColor="#3b82f6" stopOpacity={0.3}/>
                                <stop offset="95%" stopColor="#3b82f6" stopOpacity={0}/>
                              </linearGradient>
                              <linearGradient id="voltArea" x1="0" y1="0" x2="0" y2="1">
                                <stop offset="5%" stopColor="#10b981" stopOpacity={0.3}/>
                                <stop offset="95%" stopColor="#10b981" stopOpacity={0}/>
                              </linearGradient>
                              <linearGradient id="voltageArea" x1="0" y1="0" x2="0" y2="1">
                                <stop offset="5%" stopColor="#22d3ee" stopOpacity={0.3}/>
                                <stop offset="95%" stopColor="#22d3ee" stopOpacity={0}/>
                              </linearGradient>
                            </defs>
                            <CartesianGrid strokeDasharray="3 3" stroke="#e0e7ff" />
                            <XAxis dataKey="time" stroke="#6b7280" fontSize={10} tick={{ fill: '#6b7280' }} />
                            <YAxis stroke="#6b7280" fontSize={10} tick={{ fill: '#6b7280' }} />
                            <Tooltip
                              contentStyle={{ backgroundColor: theme === 'dark' ? '#23272f' : '#fff', color: theme === 'dark' ? '#f3f4f6' : '#222', border: "1px solid #e2e8f0", borderRadius: '8px' }}
                              formatter={(value, name, props) => {
                                if (name === 'Radar') {
                                  return [props.payload.radarLabel, 'Radar'];
                                }
                                return [value, name];
                              }}
                            />
                            <Legend />
                            <Area type="monotone" dataKey="temperature" stroke="#3b82f6" fill="url(#tempArea)" fillOpacity={1} name="Temperature (°C)" isAnimationActive={true} animationDuration={900} />
                            <Area type="monotone" dataKey="humidity1" stroke="#10b981" fill="url(#voltArea)" fillOpacity={1} name="Humidity 1 (%)" isAnimationActive={true} animationDuration={900} />
                            <Area
                              type="stepAfter"
                              dataKey="radar"
                              stroke="#e11d48"
                              strokeWidth={3}
                              fill="#e11d48"
                              fillOpacity={0.25}
                              name="Radar"
                              legendType="diamond"
                              isAnimationActive={true}
                              animationDuration={900}
                            />
                            <Area type="monotone" dataKey="voltage" stroke="#22d3ee" fill="url(#voltageArea)" fillOpacity={1} name="Voltage (V)" isAnimationActive={true} animationDuration={900} />
                          </AreaChart>
                        )}
                      </ResponsiveContainer>
                    )}
                  </div>
                </div>
                {/* Custom Pagination Design */}
                <div className="example small" style={{marginTop: 8, marginBottom: 4}}>
                  <div className="text" style={{cursor: chartPage > 1 ? 'pointer' : 'not-allowed', color: chartPage > 1 ? '#6a558e' : '#bbb'}} onClick={() => handlePageChange(chartPage - 1)}>previous data</div>
                  <div className="counter" style={{position: 'relative'}}>
                    <span className="number" style={{fontFamily: 'Rowdies, cursive'}}>{String(chartPage).padStart(2, '0')}</span>
                    <div className="background"></div>
                    <span className="number" style={{fontFamily: 'Rowdies, cursive'}}>{String(chartTotalPages).padStart(2, '0')}</span>
                  </div>
                  <div className="text" style={{cursor: chartPage < chartTotalPages ? 'pointer' : 'not-allowed', color: chartPage < chartTotalPages ? '#6a558e' : '#bbb'}} onClick={() => handlePageChange(chartPage + 1)}>next data</div>
                </div>
                {/* End Custom Pagination Design */}
              </>
            );
          })()}
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
                  <th>Voltage (V)</th>
                  <th>Active Button</th>
                  <th>Human Present</th>
                </tr>
              </thead>
              <AnimatePresence>
                <tbody>
                  {currentPageData.map((data, idx) => {
                    let activeButton = [];
                    if (data.button4) {
                      activeButton.push('Manual off Button active');
                    } else {
                      if (data.button1) activeButton.push('Button 1');
                      if (data.button2) activeButton.push('Button 2');
                      if (data.button3) activeButton.push('Button 3');
                    }
                    const activeButtonDisplay = activeButton.length > 0 ? activeButton.join(', ') : 'No active buttons';
                    return (
                      <motion.tr
                        key={data.id || idx}
                        initial={{ opacity: 0, y: 20 }}
                        animate={{ opacity: 1, y: 0 }}
                        exit={{ opacity: 0, y: -20 }}
                        transition={{ duration: 0.3 }}
                      >
                        <td>{(currentPage - 1) * PAGE_SIZE + idx + 1}</td>
                        <td>{new Date(data.timestamp).toLocaleTimeString()}</td>
                        <td>{new Date(data.timestamp).toLocaleDateString()}</td>
                        <td>{data.temperature}</td>
                        <td>{data.humidity1}</td>
                        <td>{data.voltage}</td>
                        <td>{activeButtonDisplay}</td>
                        <td>{data.ld2410cHumanPresent === 1 || data.ld2410cHumanPresent === true || data.ld2410cHumanPresent === 'true' ? 'Yes' : 'No'}</td>
                      </motion.tr>
                    );
                  })}
                </tbody>
              </AnimatePresence>
            </table>
            <div className="pagination-controls">
              <Pagination pages={totalPages} page={currentPage} onPageChange={handlePageChange} />
            </div>
          </div>
        </section>
      </div>
    </div>
  );
};

export default Dashboard;
