Register-ArgumentCompleter -Native -CommandName awfan -ScriptBlock {
    param($wordToComplete, $commandAst, $cursorPosition)

    $commands = @(
        'status',
        'fans',
        'temps',
        'watch',
        'profiles',
        'presets',
        'doctor',
        'state',
        'clear-state',
        'boost',
        'max',
        'profile',
        'auto',
        'broker-status',
        'update',
        'raw-probe',
        'exact-probe',
        'probe',
        'inspect-awcc',
        'version',
        'help'
    )

    $tokens = @($commandAst.CommandElements | ForEach-Object { $_.Extent.Text })

    if ($tokens.Count -le 2) {
        $commands |
            Where-Object { $_ -like "$wordToComplete*" } |
            ForEach-Object {
                [System.Management.Automation.CompletionResult]::new(
                    $_,
                    $_,
                    'ParameterValue',
                    $_
                )
            }
        return
    }

    $command = $tokens[1]

    if ($command -in @('profile', 'auto') -and $tokens.Count -le 3) {
        @('1', '2', '3', '4', '5') |
            Where-Object { $_ -like "$wordToComplete*" } |
            ForEach-Object {
                [System.Management.Automation.CompletionResult]::new(
                    $_,
                    $_,
                    'ParameterValue',
                    "Firmware profile index $_"
                )
            }
        return
    }

    if ($command -eq 'update') {
        @('--check', '--force') |
            Where-Object { $_ -like "$wordToComplete*" } |
            ForEach-Object {
                [System.Management.Automation.CompletionResult]::new(
                    $_,
                    $_,
                    'ParameterName',
                    $_
                )
            }
        return
    }

    $options = @('--json', '--yes')

    if ($command -in @('probe', 'inspect-awcc')) {
        $options += '--namespace'
    }
    if ($command -eq 'probe') {
        $options += '--all'
        $options += '--signatures'
    }

    $options |
        Where-Object { $_ -like "$wordToComplete*" } |
        Sort-Object -Unique |
        ForEach-Object {
            [System.Management.Automation.CompletionResult]::new(
                $_,
                $_,
                'ParameterName',
                $_
            )
        }
}
