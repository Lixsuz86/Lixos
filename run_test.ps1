$qemu = "C:/msys64/mingw64/bin/qemu-system-x86_64.exe"
$args = @(
    "-cpu", "max",
    "-cdrom", "build/lixos.iso",
    "-drive", "id=disk0,file=build/disk.img,if=none,format=raw",
    "-device", "ahci,id=ahci",
    "-device", "ide-hd,drive=disk0,bus=ahci.0",
    "-device", "VGA,edid=on,xres=1920,yres=1080",
    "-netdev", "user,id=net0",
    "-device", "e1000,netdev=net0,mac=52:54:00:12:34:56",
    "-serial", "file:build/serial.log",
    "-m", "512M",
    "-display", "none",
    "-qmp", "tcp:localhost:4444,server,nowait"
)

Remove-Item -Path "build/serial.log" -ErrorAction SilentlyContinue

$proc = Start-Process -FilePath $qemu -ArgumentList $args -PassThru
Start-Sleep -Seconds 25

# Connect to QMP monitor to take screenshot if possible
try {
    $client = New-Object System.Net.Sockets.TcpClient("localhost", 4444)
    $stream = $client.GetStream()
    $writer = New-Object System.IO.StreamWriter($stream)
    $reader = New-Object System.IO.StreamReader($stream)
    
    # Read QMP greeting
    $line = $reader.ReadLine()
    
    # QMP handshake
    $writer.WriteLine('{"execute":"qmp_capabilities"}')
    $writer.Flush()
    $resp = $reader.ReadLine()
    
    # Take screenshot
    $writer.WriteLine('{"execute":"screendump","arguments":{"filename":"build/screen.ppm"}}')
    $writer.Flush()
    Start-Sleep -Milliseconds 500
    $resp2 = $reader.ReadLine()

    $writer.Close()
    $reader.Close()
    $client.Close()

    # Automatically convert PPM screenshot to PNG
    if (Test-Path "build/screen.ppm") {
        python -c "from PIL import Image; Image.open('build/screen.ppm').save('build/screen.png')"
        Write-Host "Screenshot converted: build/screen.png"
    }
} catch {
    Write-Host "QMP screenshot skipped: $_"
}

Start-Sleep -Seconds 2
Stop-Process -Id $proc.Id -Force
Write-Host "QEMU test completed."
