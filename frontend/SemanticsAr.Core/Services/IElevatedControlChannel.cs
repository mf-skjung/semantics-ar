using SemanticsAr.Core.Domain;

namespace SemanticsAr.Core.Services;

public interface IElevatedControlChannel : IDisposable
{
    ElevatedError LoadCatalog(out IReadOnlyList<RecoverableItem> items);

    ElevatedError LoadPreserved(out IReadOnlyList<RecoverableItem> items);

    RecoveryOutcome Recover(RecoverableItem item, string targetPath);

    ElevatedError SetMode(uint mode);

    ElevatedError SetBudget(ulong retention100ns, ulong capacityBytes);
}
