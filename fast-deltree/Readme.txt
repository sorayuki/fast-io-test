Remove a directory with 112606 files, 10043 subdirectories, in memory disk in NTFS created by ImDisk:

PS E:\> Measure-Command { cmd.exe /c "echo y| rd /s boost_1" }

Days              : 0
Hours             : 0
Minutes           : 0
Seconds           : 4
Milliseconds      : 206
Ticks             : 42064210
TotalDays         : 4.86854282407407E-05
TotalHours        : 0.00116845027777778
TotalMinutes      : 0.0701070166666667
TotalSeconds      : 4.206421
TotalMilliseconds : 4206.421

PS E:\> Measure-Command { Remove-Item -Recurse boost_2 }

Days              : 0
Hours             : 0
Minutes           : 0
Seconds           : 5
Milliseconds      : 291
Ticks             : 52911010
TotalDays         : 6.12395949074074E-05
TotalHours        : 0.00146975027777778
TotalMinutes      : 0.0881850166666667
TotalSeconds      : 5.291101
TotalMilliseconds : 5291.101

PS E:\> Measure-Command { robocopy /MIR /LOG:nul /MT:64 empty boost_3
>> rd boost_3
>> }

Days              : 0
Hours             : 0
Minutes           : 0
Seconds           : 7
Milliseconds      : 959
Ticks             : 79594945
TotalDays         : 9.21237789351852E-05
TotalHours        : 0.00221097069444444
TotalMinutes      : 0.132658241666667
TotalSeconds      : 7.9594945
TotalMilliseconds : 7959.4945

PS E:\> Measure-Command { D:\devspace\fastrd\build\Release\fastrd.exe boost_4 }

Days              : 0
Hours             : 0
Minutes           : 0
Seconds           : 2
Milliseconds      : 502
Ticks             : 25028046
TotalDays         : 2.89676458333333E-05
TotalHours        : 0.0006952235
TotalMinutes      : 0.04171341
TotalSeconds      : 2.5028046
TotalMilliseconds : 2502.8046

PS E:\> Measure-Command { D:\devspace\fastrd\build\Release\fastrd.exe boost_2 } // use filesystem::remove_all

Days              : 0
Hours             : 0
Minutes           : 0
Seconds           : 4
Milliseconds      : 647
Ticks             : 46470942
TotalDays         : 5.37858125E-05
TotalHours        : 0.0012908595
TotalMinutes      : 0.07745157
TotalSeconds      : 4.6470942
TotalMilliseconds : 4647.0942

PS E:\> Measure-Command { D:\devspace\fastrd\build\Release\fastrd.exe boost_3 } // use SHFileOperationW

Days              : 0
Hours             : 0
Minutes           : 7
Seconds           : 33
Milliseconds      : 757
Ticks             : 4537577271
TotalDays         : 0.00525182554513889
TotalHours        : 0.126043813083333
TotalMinutes      : 7.562628785
TotalSeconds      : 453.7577271
TotalMilliseconds : 453757.7271

PS E:\>

fastcopy:
TotalDel   = 3,777 MiB
DelFiles   = 112,606 (10,044)
TotalTime  = 4.1 sec
FileRate   = 27,505 files/s