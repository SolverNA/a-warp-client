sc stop AWARPTunnel$AWARP
sc delete AWARPTunnel$AWARP
taskkill /IM "AWARP-service.exe" /F
taskkill /IM "AWARP.exe" /F
exit /b 0
