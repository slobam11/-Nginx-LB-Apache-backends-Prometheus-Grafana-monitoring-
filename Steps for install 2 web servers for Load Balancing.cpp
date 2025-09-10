So this is steps i made for creatiing LOAD BALANCER between two servers : 
# I use Ubuntu 24.04 LTS and Alma Linux version 9. 

On Ubuntu server you can install nginx 
On Alma Linux you can install httpd 

1. Backend Server Configuration (AlmaLinux)
1.1 Apache Virtual Hosts

Files in /etc/httpd/conf.d/:

server1.conf

<VirtualHost *:8083>
    DocumentRoot "/var/www/server1"
</VirtualHost>


server2.conf

<VirtualHost *:8084>
    DocumentRoot "/var/www/server2"
</VirtualHost>

Add content:
sudo mkdir -p /var/www/server1 /var/www/server2
echo "<h1>Web Server 1 - AlmaLinux</h1>" | sudo tee /var/www/server1/index.html
echo "<h1>Web Server 2 - AlmaLinux</h1>" | sudo tee /var/www/server2/index.html


This command : Explanation : 
sudo mkdir -p /var/www/server1 /var/www/server2
This command creates two directories: /var/www/server1 and /var/www/server2.
The -p option ensures that parent directories are created if they don't exist.
sudo is used to run the command with administrative (root) privileges.

echo "<h1>Web Server 1 - AlmaLinux</h1>" | sudo tee /var/www/server1/index.html
This command writes a simple HTML heading into the file /var/www/server1/index.html.
It uses tee with sudo to write the file with root permissions.

echo "<h1>Web Server 2 - AlmaLinux</h1>" | sudo tee /var/www/server2/index.html
Similarly, this writes a different HTML heading into /var/www/server2/index.html.

In summary:
These commands create two web server directories and place a basic index.html file in each, showing a heading for "Web Server 1" and "Web Server 2" respectively.


# Restart Apache:

sudo systemctl restart httpd

Test:
curl http://localhost:8083
curl http://localhost:8084

2.2 Apache Status Endpoint

/etc/httpd/conf.d/status.conf

<Location "/server-status">
  SetHandler server-status
  Require ip 127.0.0.1 192.168.1.30
</Location>
ExtendedStatus On


Restart:

sudo systemctl restart httpd
curl http://127.0.0.1/server-status?auto

2.3 Apache Exporter

Install:

cd /tmp
wget https://github.com/Lusitaniae/apache_exporter/releases/download/v0.13.4/apache_exporter-0.13.4.linux-amd64.tar.gz
tar -xzf apache_exporter-*.tar.gz
sudo mv apache_exporter-*/apache_exporter /usr/local/bin/
sudo chmod +x /usr/local/bin/apache_exporter


Systemd unit /etc/systemd/system/apache_exporter.service:

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


Start:

sudo systemctl daemon-reload
sudo systemctl enable --now apache_exporter


Test:

curl http://127.0.0.1:9117/metrics | grep apache_

3. Load Balancer Configuration (Ubuntu)
3.1 Nginx Load Balancer

File /etc/nginx/conf.d/loadbalancer.conf:

upstream backend_servers {
    server 192.168.1.5:8083;
    server 192.168.1.5:8084;
}

server {
    listen 80;
    location / {
        proxy_pass http://backend_servers;
    }
}


Restart:

sudo nginx -t
sudo systemctl restart nginx


Test:

curl http://localhost

3.2 Nginx Stub Status

/etc/nginx/conf.d/status.conf:

server {
  listen 127.0.0.1:8089;
  server_name localhost;

  location /stub_status {
    stub_status;
    allow 127.0.0.1;
    deny all;
  }
}


Test:

curl http://127.0.0.1:8089/stub_status

3.3 Nginx Exporter

Install:

sudo apt install -y prometheus-nginx-exporter


Systemd override (/etc/systemd/system/prometheus-nginx-exporter.service.d/override.conf):

[Service]
ExecStart=
ExecStart=/usr/bin/prometheus-nginx-exporter \
  --nginx.scrape-uri=http://127.0.0.1:8089/stub_status \
  --web.listen-address=:9113


Start:

sudo systemctl daemon-reload
sudo systemctl restart prometheus-nginx-exporter


Test:

curl http://127.0.0.1:9113/metrics | grep nginx_

4. Prometheus Configuration 

In direction =>  /etc/prometheus/prometheus.yml (relevant part): you need to do some configurations : To add config nginx and apache that prometheus can follow them.


scrape_configs:
  - job_name: 'prometheus'
    static_configs:
      - targets: ['localhost:9090']

  - job_name: 'nginx_lb'
    static_configs:
      - targets: ['192.168.1.30:9113']

  - job_name: 'apache_backends'
    static_configs:
      - targets: ['192.168.1.5:9117']


Restart:

sudo systemctl restart prometheus


Check in Prometheus web UI: /targets.

5. Grafana Dashboard
5.1 Nginx (LB) Panels

Requests/s

rate(nginx_http_requests_total[1m])


Active connections

nginx_connections_active


Accepted connections/s

rate(nginx_connections_accepted[1m])


Reading/Writing/Waiting

nginx_connections_reading
nginx_connections_writing
nginx_connections_waiting

5.2 Apache (Backend) Panels

Requests/s per backend

sum(rate(apache_accesses_total[1m])) by (instance)


Busy/Idle workers

sum(apache_scoreboard{state=~"busy|idle"}) by (state, instance)


Bytes/s

sum(rate(apache_bytes_total[1m])) by (instance)


Uptime

apache_uptime_seconds_total

6. Load Balancing Test

Generate traffic:

ab -n 2000 -c 50 http://192.168.1.30/


In Grafana, observe:

Nginx nginx_http_requests_total increasing.

Apache apache_accesses_total increasing.

Busy workers oscillating.
