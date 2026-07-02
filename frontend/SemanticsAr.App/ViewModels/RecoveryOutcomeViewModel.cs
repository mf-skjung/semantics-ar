using SemanticsAr.Core.Domain;

namespace SemanticsAr.App.ViewModels;

public sealed class RecoveryOutcomeViewModel
{
    public RecoveryOutcomeViewModel(RecoveryOutcome outcome)
    {
        DisplayPath = outcome.Item.ProvenancePath;
        StatusText = outcome.Kind switch
        {
            RecoveryOutcomeKind.RestoredVerified =>
                "Restored — byte-for-byte verified.",
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
}
