using System;
using SemanticsAr.Core.Domain;

namespace SemanticsAr.App.ViewModels;

public sealed class RecoveryOutcomeViewModel
{
    public RecoveryOutcomeViewModel(RecoveryOutcome outcome)
    {
        DisplayPath = outcome.Item.ProvenancePath;

        bool sideBySide = outcome.TargetPath.Length > 0
            && !string.Equals(outcome.TargetPath, outcome.Item.ProvenancePath, StringComparison.OrdinalIgnoreCase);

        StatusText = outcome.Kind switch
        {
            RecoveryOutcomeKind.RestoredVerified => sideBySide
                ? $"Restored alongside as {Leaf(outcome.TargetPath)} — byte-for-byte verified."
                : "Restored — byte-for-byte verified.",
            RecoveryOutcomeKind.DeclinedLeftIntact =>
                $"Declined — your current file was left intact (status {outcome.KernelResult}).",
            _ =>
                $"Could not run — {outcome.Error}.",
        };
        Verified = outcome.Kind == RecoveryOutcomeKind.RestoredVerified;
    }

    public string DisplayPath { get; }

    public string StatusText { get; }

    public bool Verified { get; }

    private static string Leaf(string path)
    {
        int i = path.LastIndexOf('\\');
        return i >= 0 && i < path.Length - 1 ? path[(i + 1)..] : path;
    }
}
