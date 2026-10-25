param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Paths
)
$scriptPath = Join-Path $PSScriptRoot 'format_c_functions.py'
if (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 $scriptPath @Paths
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    & python $scriptPath @Paths
} else {
    Write-Error 'Python 3 is required.'
    exit 1
}
exit $LASTEXITCODE
