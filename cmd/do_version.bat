@echo off
SetLocal EnableDelayedExpansion

echo -------------------- VERSION: %3 --------------------

echo Indexing version %2/local/%3
%1 --log-level 0 --create-version-index "%2/%3.lvi" --version "%2/local/%3"
if %errorlevel% neq 0 exit /b %errorlevel%

echo Creating content index for unknown content of version %2/local/%3
%1 --log-level 0 --create-content-index "%2/%3.lci" --content-index "%2/chunks.lci" --version-index "%2/%3.lvi" --version "%2/local/%3"
if %errorlevel% neq 0 exit /b %errorlevel%

echo Creating chunks for unknown content of version %2/local/%3
%1 --log-level 0 --create-content "%2/%3_chunks" --content-index "%2/%3.lci" --version "%2/local/%3" --version-index "%2/%3.lvi"
if %errorlevel% neq 0 exit /b %errorlevel%

echo Adding the new chunks from %2/%3_chunks to %2/chunks\
IF NOT EXIST "%2\chunks\" mkdir "%2\chunks"
xcopy /s %2\%3_chunks\* %2\chunks > nul

echo Merging the new chunks from %2/local/%3.lci into %2/chunks.lci
%1 --log-level 0 --create-content-index "%2/chunks.lci" --content-index "%2/chunks.lci" --merge-content-index "%2/%3.lci"
if %errorlevel% neq 0 exit /b %errorlevel%

echo Updating %2/remote/incremental
rem IF NOT EXIST "%2\remote\incremental.lvi" (
rem     echo Creating index for %2/remote/incremental
rem     %1 --log-level 0 --create-version-index "%2\remote\incremental.lvi" --version "%2\remote\incremental"
rem )

rem %1 --log-level 0 --update-version "%2/remote/incremental" --version-index "%2/remote/incremental.lvi" --content "%2/chunks" --content-index "%2/chunks.lci" --target-version-index "%2/%3.lvi"
%1 --log-level 0 --update-version "%2/remote/incremental" --content "%2/chunks" --content-index "%2/chunks.lci" --target-version-index "%2/%3.lvi"
if %errorlevel% neq 0 exit /b %errorlevel%

rem echo Updating index "%2/remote/incremental.lvi" from "%2/%3.lvi"
rem copy "%2/%3.lvi" "%2/remote/incremental.lvi"

echo Creating %2/remote/%3
rem %1 --log-level 0 --create-version "%2/remote/%3" --version-index "%2/%3.lvi" --content "%2/chunks" --content-index "%2/chunks.lci" --version "%2/remote/%3"
if %errorlevel% neq 0 exit /b %errorlevel%
