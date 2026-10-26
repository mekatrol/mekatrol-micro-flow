param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Arguments
)
$scriptPath = Join-Path $PSScriptRoot 'format_c_functions.py'
$workspacePath = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $workspacePath 'src'

if (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 $scriptPath $sourcePath @Arguments
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    & python $scriptPath $sourcePath @Arguments
} else {
    Write-Error 'Python 3 is required.'
    exit 1
}
exit $LASTEXITCODE
