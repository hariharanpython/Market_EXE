const socket = new WebSocket('ws://localhost:9002');
const statusEl = document.getElementById('status');
const statusTextEl = statusEl.querySelector('.status-text');

// --- TREND DATA ---
const priceHistory = {
    'EURUSD': [],
    'GBPUSD': [],
    'USDJPY': []
};
const MAX_HISTORY = 30;

// --- BACKGROUND ANIMATION ---
const canvas = document.getElementById('bg-canvas');
const ctx = canvas.getContext('2d');

let width, height;
let particles = [];

function initCanvas() {
    width = canvas.width = window.innerWidth;
    height = canvas.height = window.innerHeight;
    particles = [];
    for (let i = 0; i < 50; i++) {
        particles.push({
            x: Math.random() * width,
            y: Math.random() * height,
            vx: (Math.random() - 0.5) * 1,
            vy: (Math.random() - 0.5) * 0.5,
            size: Math.random() * 2 + 1
        });
    }
}

function animate() {
    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = '#38bdf8';
    ctx.strokeStyle = '#38bdf8';

    particles.forEach(p => {
        p.x += p.vx;
        p.y += p.vy;

        if (p.x < 0) p.x = width;
        if (p.x > width) p.x = 0;
        if (p.y < 0) p.y = height;
        if (p.y > height) p.y = 0;

        ctx.beginPath();
        ctx.arc(p.x, p.y, p.size, 0, Math.PI * 2);
        ctx.fill();
    });

    ctx.lineWidth = 0.5;
    for (let i = 0; i < particles.length; i++) {
        for (let j = i + 1; j < particles.length; j++) {
            let dx = particles[i].x - particles[j].x;
            let dy = particles[i].y - particles[j].y;
            let dist = Math.sqrt(dx * dx + dy * dy);
            if (dist < 150) {
                ctx.globalAlpha = 1 - (dist / 150);
                ctx.beginPath();
                ctx.moveTo(particles[i].x, particles[i].y);
                ctx.lineTo(particles[j].x, particles[j].y);
                ctx.stroke();
            }
        }
    }
    ctx.globalAlpha = 1.0;
    requestAnimationFrame(animate);
}

window.addEventListener('resize', initCanvas);
initCanvas();
animate();

// --- SPARKLINE RENDERING ---
function drawSparkline(symbol, data) {
    const canvas = document.getElementById(`spark-${symbol}`);
    if (!canvas) return;

    const sctx = canvas.getContext('2d');
    const w = canvas.width = canvas.offsetWidth;
    const h = canvas.height = canvas.offsetHeight;

    if (data.length < 2) return;

    const min = Math.min(...data);
    const max = Math.max(...data);
    const range = max - min || 1;

    sctx.clearRect(0, 0, w, h);
    sctx.beginPath();
    sctx.strokeStyle = '#38bdf8';
    sctx.lineWidth = 2;
    sctx.lineJoin = 'round';

    for (let i = 0; i < data.length; i++) {
        const x = (i / (MAX_HISTORY - 1)) * w;
        const y = h - ((data[i] - min) / range) * (h - 4) - 2;
        if (i === 0) sctx.moveTo(x, y);
        else sctx.lineTo(x, y);
    }
    sctx.stroke();

    // Fill area under line
    sctx.lineTo(w, h);
    sctx.lineTo(0, h);
    sctx.globalAlpha = 0.1;
    sctx.fillStyle = '#38bdf8';
    sctx.fill();
    sctx.globalAlpha = 1.0;
}

// --- WEBSOCKET HANDLING ---
socket.onopen = () => {
    statusEl.className = 'status-pill CONNECTED';
    statusTextEl.textContent = 'Connected';
};

socket.onclose = () => {
    statusEl.className = 'status-pill DISCONNECTED';
    statusTextEl.textContent = 'Disconnected';
};

socket.onmessage = (event) => {
    try {
        const data = JSON.parse(event.data);
        const row = document.getElementById(data.symbol);

        if (row) {
            const bidEl = row.querySelector('.bid');
            const askEl = row.querySelector('.ask');
            const lastUpdateEl = row.querySelector('.last-update');

            // Update sparkline data
            const history = priceHistory[data.symbol];
            const midPrice = (data.bid + data.ask) / 2;
            history.push(midPrice);
            if (history.length > MAX_HISTORY) history.shift();
            drawSparkline(data.symbol, history);

            processUpdate(row, bidEl, data.bid);
            processUpdate(row, askEl, data.ask);

            const date = new Date(data.timestamp * 1000);
            lastUpdateEl.textContent = date.toLocaleTimeString();

            // Trigger strong visual heartbeat pulse every second
            // This clearly communicates "refresh happened" to the user
            row.classList.remove('pulse-neutral');
            void row.offsetWidth; // Trigger reflow
            row.classList.add('pulse-neutral');
        }
    } catch (err) {
        console.error('Error parsing WebSocket message:', err);
    }
};

function processUpdate(row, el, newPrice) {
    const isBid = el.classList.contains('bid');
    const isAsk = el.classList.contains('ask');
    const oldPriceStr = el.textContent;
    const oldPrice = parseFloat(oldPriceStr);
    const priceStr = newPrice.toFixed(5);

    el.textContent = priceStr;

    if (oldPriceStr !== '-' && !isNaN(oldPrice)) {
        // Reset classes safely
        el.className = `price-cell ${isBid ? 'bid' : 'ask'}`;

        if (newPrice > oldPrice) {
            el.classList.add('price-up');
            triggerRowFlash(row, 'pulse-up');
        } else if (newPrice < oldPrice) {
            el.classList.add('price-down');
            triggerRowFlash(row, 'pulse-down');
        }
    } else {
        el.className = `price-cell ${isBid ? 'bid' : 'ask'}`;
    }
}

function triggerRowFlash(row, className) {
    row.classList.remove('pulse-up', 'pulse-down');
    void row.offsetWidth; // Trigger reflow
    row.classList.add(className);
}
