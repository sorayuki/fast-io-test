Set-Location .\build\Release
$blocksizes = (256, 1024, 4096, 16384, 32768)

foreach($mode in (0, 1, 2, 3, 4)) {
    Write-Host "======== mode $mode ========"
    Write-Host -NoNewline "MBps"
    foreach ($blocksize in $blocksizes) {
        Write-Host -NoNewline "`t$blocksize"
    }
    Write-Host "`tblocksize(KB)"
    foreach ($thread in (1,2,4,8)) {
        Write-Host -NoNewline "$thread"
        foreach ($blocksize in $blocksizes) {
            D:\tools\SysinternalsSuite\RAMMap64.exe -Et
            Start-Sleep -Seconds 5
            $output = .\fast-read $mode $thread $blocksize
            $speed = $null
            if ("$output" -match '([\d\.]+) MB' -and $matches) {
                $speed = $matches[1]
                Write-Host -NoNewline "`t$speed"
            } else {
                Write-Host "$output"
            }
        }
        Write-Host ""
    }
    Write-Host "threads"
}