Hello dear people , this is a little guide from me how to add nginx Load Balancer with Apache Backends + Prometheus/Grafan . 

HEre are the step's : 

# Two servers : 
I use Ubuntu 24.04 LTS and Alma Linux 9


# Nginx Load Balancer + Apache Backends + Prometheus/Grafana

End-to-end lab setup:
- **Ubuntu (Load Balancer)**: Nginx as reverse proxy / LB
- **AlmaLinux (Backend)**: Apache httpd on ports 8083 & 8084
- **Prometheus & Grafana**: metrics from Nginx exporter & Apache exporter (+ Node exporter)

> ⚠️ Sanitize before publishing: replace private IPs with placeholders like `BACKEND_IP` / `LB_IP`.


## 1) Architecture

- LB (Ubuntu):
  - Nginx upstream → `BACKEND_IP:8083`, `BACKEND_IP:8084`
  - `stub_status` at `127.0.0.1:8089`
  - `prometheus-nginx-exporter` at `:9113`

- Backend (AlmaLinux):
  - Apache vhosts on `:8083` and `:8084`
  - `mod_status` exposed locally at `/server-status?auto`
  - `apache_exporter` at `:9117`

- Prometheus (on LB) scrapes:
  - Nginx exporter (`LB_IP:9113`)
  - Apache exporter (`BACKEND_IP:9117`)
  - Node exporters

- Grafana visualizes Prometheus metrics


## 2) Backend (AlmaLinux) — Apache

### 2.1 Virtual Hosts
`/etc/httpd/conf.d/server1.conf`
apache
<VirtualHost *:8083>
    DocumentRoot "/var/www/server1"
</VirtualHost>

Alma linux : in directions /etc/httpd/conf.d/server2.conf 
add

<VirtualHost *:8084>
    DocumentRoot "/var/www/server2"
</VirtualHost>

Create a content like : 
sudo mkdir -p /var/www/server1 /var/www/server2
echo "<h1>Web Server 1 - AlmaLinux</h1>" | sudo tee /var/www/server1/index.html
echo "<h1>Web Server 2 - AlmaLinux</h1>" | sudo tee /var/www/server2/index.html

Listen ports (in /etc/httpd/conf/httpd.conf):

Listen 8083
Listen 8084


Disable welcome page (optional):

sudo mv /etc/httpd/conf.d/welcome.conf /etc/httpd/conf.d/welcome.conf.bak


Restart & test:

sudo systemctl restart httpd
curl http://localhost:8083
curl http://localhost:8084

2.2 mod_status

/etc/httpd/conf.d/status.conf

<Location "/server-status">
  SetHandler server-status
  Require ip 127.0.0.1 LB_IP
</Location>
ExtendedStatus On


For strict local use only: Require ip 127.0.0.1

sudo systemctl restart httpd
curl http://127.0.0.1/server-status?auto

2.3 Apache Exporter (systemd)

Download & install:

cd /tmp
wget https://github.com/Lusitaniae/apache_exporter/releases/download/v0.13.4/apache_exporter-0.13.4.linux-amd64.tar.gz
tar -xzf apache_exporter-*.tar.gz
sudo mv apache_exporter-*/apache_exporter /usr/local/bin/
sudo chmod +x /usr/local/bin/apache_exporter


/etc/systemd/system/apache_exporter.service

[Unit]
Description=Apache Prometheus Exporter
After=network-online.target httpd.service
Wants=network-online.target

[Service]
User=nobody
Group=nobody
ExecStart=/usr/local/bin/apache_exporter \
  --telemetry.address=:9117 \
  --scrape_uri=http://127.0.0.1/server-status?auto
Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target


Start & test:

sudo systemctl daemon-reload
sudo systemctl enable --now apache_exporter
sudo systemctl status apache_exporter --no-pager
curl -s http://127.0.0.1:9117/metrics | grep apache_ | head


Firewall (if Prometheus scrapes remotely):

sudo firewall-cmd --permanent --add-port=9117/tcp && sudo firewall-cmd --reload

3) Load Balancer (Ubuntu) — Nginx
3.1 Upstream + proxy

/etc/nginx/conf.d/loadbalancer.conf

upstream backend_servers {
    server BACKEND_IP:8083;
    server BACKEND_IP:8084;
}
server {
    listen 80;
    location / {
        proxy_pass http://backend_servers;
    }
}


Test:

sudo nginx -t && sudo systemctl restart nginx
curl http://localhost

3.2 stub_status

/etc/nginx/conf.d/status.conf

server {
  listen 127.0.0.1:8089;
  server_name localhost;
  location /stub_status {
    stub_status;
    allow 127.0.0.1;
    deny all;
  }
}

sudo nginx -t && sudo systemctl reload nginx
curl http://127.0.0.1:8089/stub_status

3.3 Nginx Exporter (systemd override)

Install exporter:

sudo apt update
sudo apt install -y prometheus-nginx-exporter


Override ExecStart:
/etc/systemd/system/prometheus-nginx-exporter.service.d/override.conf

[Service]
ExecStart=
ExecStart=/usr/bin/prometheus-nginx-exporter \
  --nginx.scrape-uri=http://127.0.0.1:8089/stub_status \
  --web.listen-address=:9113


Reload & test:

sudo systemctl daemon-reload
sudo systemctl restart prometheus-nginx-exporter
sudo systemctl status prometheus-nginx-exporter --no-pager
curl -s http://127.0.0.1:9113/metrics | grep nginx_ | head

4) Prometheus Scrape Config

Append to /etc/prometheus/prometheus.yml:

scrape_configs:
  - job_name: 'nginx_lb'
    static_configs:
      - targets: ['LB_IP:9113']

  - job_name: 'apache_backends'
    static_configs:
      - targets: ['BACKEND_IP:9117']

  - job_name: 'node'
    static_configs:
      - targets: ['LB_IP:9100','BACKEND_IP:9100']  # adjust list as needed


Restart & verify:
sudo systemctl restart prometheus
# open http://LB_IP:9090/targets → jobs should be UP

5) Grafana Panels (PromQL)
Nginx / LB
Requests/s
x_http_requests_total[1m])


Active connections
nginx_connections_active


Accepted connections/s
rate(nginx_connections_accepted[1m])


Reading/Writing/Waiting
nginx_connections_reading
nginx_connections_writing
nginx_connections_waiting


Apache / Backend
Requests/s per backend
sum(rate(apache_accesses_total[1m])) by (instance)


Busy vs Idle
sum(apache_scoreboard{state=~"busy|idle"}) by (state, instance)


Bytes/s
sum(rate(apache_bytes_total[1m])) by (instance)


Uptime
apache_uptime_seconds_total

6) Load Test (optional)
ab -n 2000 -c 50 http://LB_IP/  ( Ubuntu Server ) 
# or wrk -t2 -c50 -d30s http://LB_IP/

7) Troubleshooting (quick)
Port 80 in use on LB → stop Apache on LB: sudo systemctl stop apache2
No nginx_ metrics → check stub_status and exporter args
Apache exporter 216/GROUP on RHEL/Alma → use Group=nobody
No data in Grafana → check datasource, time range, and Prometheus /targets

To add on Grafana dashboard go in new dashboard and go to import dashboar and add number 12078 that is a number for nginx dashboard in side grafana.
<img width="1619" height="915" alt="image" src="https://github.com/user-attachments/assets/01351141-cd2d-4d6b-a3eb-597c319290ec" /> You can see that in prometheus/targets and nginx and apache is up , i need to mark my ip ,sorry :) 
<img width="1452" height="1177" alt="image" src="https://github.com/user-attachments/assets/dd8f634c-8c4e-4059-8778-d24a18b097d8" /> IN grafana everehything os up and green :) 
<img width="1198" height="31" alt="image" src="https://github.com/user-attachments/assets/e69afeb4-6c51-4b56-8eef-8fe4a0769bb3" />

End : So this is how i do LB - Is a software or hardwe that can be infront of morw and more backend server and take out of all traffic from client . If one server broke down the second one is on stage :) 

# Author # 
Slobodan Milojevic 






