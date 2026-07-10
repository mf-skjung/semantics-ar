using System;
using SemanticsAr.Core.Domain;

namespace SemanticsAr.App.ViewModels;

public sealed class RecoveryOutcomeViewModel
{
    public RecoveryOutcomeViewModel(RecoveryOutcome outcome)
    {
        DisplayPath = outcome.Item.ProvenancePath;

        bool toFolder = outcome.TargetPath.Length > 0
            && !string.Equals(outcome.TargetPath, outcome.Item.ProvenancePath, StringComparison.OrdinalIgnoreCase);

        StatusText = outcome.DeclineReason switch
        {
            RecoveryDeclineReason.DefinitiveFolderOnly =>
                "Left out of the folder copy — a verified reconstruction restores to its original location. "
                + "Choose “Restore to original locations” to recover it.",
            RecoveryDeclineReason.PathBlocked =>
                "Not restored — a folder now occupies its original location; your kept copy is unchanged.",
            RecoveryDeclineReason.PathUnavailable =>
                "Not restored — a recovery-folder path could not be prepared; your kept copy is unchanged.",
            _ => outcome.Kind switch
            {
                RecoveryOutcomeKind.RestoredVerified => toFolder
                    ? $"Copied to {Dir(outcome.TargetPath)} — byte-for-byte verified."
                    : "Restored to its original location — byte-for-byte verified.",
                RecoveryOutcomeKind.DeclinedLeftIntact =>
                    $"Declined — your current file was left intact (status {outcome.KernelResult}).",
                _ =>
                    $"Could not run — {outcome.Error}.",
            },
        };

        Verified = outcome.Kind == RecoveryOutcomeKind.RestoredVerified;
    }

    public string DisplayPath { get; }

    public string StatusText { get; }

    public bool Verified { get; }

    private static string Dir(string path)
    {
        int i = path.LastIndexOf('\\');
        return i > 0 ? path[..i] : path;
    }
}
