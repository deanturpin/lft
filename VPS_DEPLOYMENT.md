# LFT VPS Deployment Guide

Complete guide for deploying LFT (Low Frequency Trader) on your VPS.

## VPS Details

- **Server**: 87.106.148.67
- **User**: sorn
- **Location**: /home/sorn/lft

## Prerequisites

✅ Already completed:
- VPS setup with Ubuntu 24.04
- GCC 14 installed for C++23 support
- Git repository cloned to `/home/sorn/lft`
- Binary built successfully
- Systemd service configured

## Step 1: Set Alpaca API Keys

Create the environment file with your Alpaca credentials:

```bash
ssh root@87.106.148.67

# Create environment file
cat > /root/.lft-env << 'ENVEOF'
ALPACA_API_KEY=your_actual_key_here
ALPACA_API_SECRET=your_actual_secret_here
ENVEOF

# Secure the file
chmod 600 /root/.lft-env
```

## Step 2: Enable and Start the Service

```bash
# Reload systemd to pick up the new service
systemctl daemon-reload

# Enable LFT to start on boot
systemctl enable lft

# Start LFT
systemctl start lft

# Check status
systemctl status lft
```

## Step 3: Monitor the Service

### View Live Logs

```bash
# Follow logs in real-time
journalctl -u lft -f

# View last 100 lines
journalctl -u lft -n 100

# View logs from today
journalctl -u lft --since today
```

## Step 4: Deploy Updates

When you push updates to the main branch:

```bash
ssh root@87.106.148.67
su - sorn
cd /home/sorn/lft
git pull origin main
cd build && make -j4
exit
systemctl restart lft
journalctl -u lft -f
```

## Makefile Commands

Add these to your local Makefile:

```makefile
deploy-vps:
	@ssh root@87.106.148.67 'su - sorn -c "cd /home/sorn/lft && git pull && cd build && make -j4" && systemctl restart lft'
	@echo "✅ Deployed"

logs-vps:
	@ssh root@87.106.148.67 'journalctl -u lft -f'

status-vps:
	@ssh root@87.106.148.67 'systemctl status lft'
```

## Troubleshooting

Check logs:
```bash
journalctl -u lft -n 50
```

Manual test:
```bash
su - sorn
cd /home/sorn/lft
export $(cat /root/.lft-env | xargs)
./build/lft
```
