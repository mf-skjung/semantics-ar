using CommunityToolkit.Mvvm.ComponentModel;
using SemanticsAr.Core.Domain;
using Chip = SemanticsAr.App.Controls.CertaintyRung;

namespace SemanticsAr.App.ViewModels;

public partial class RecoverableItemViewModel : ObservableObject
{
    public RecoverableItemViewModel(RecoverableItem model, RestoreDisposition disposition, string groupLabel)
    {
        Model = model;
        Disposition = disposition;
        GroupLabel = groupLabel;
        _isSelected = disposition is RestoreDisposition.Additive or RestoreDisposition.DeletedSince;
    }

    public RecoverableItem Model { get; }

    public RestoreDisposition Disposition { get; }

    public string GroupLabel { get; }

    [ObservableProperty]
    private bool _isSelected;

    public bool CanSelect => Disposition != RestoreDisposition.Blocked;

    public Chip Rung => Model.Rung switch
    {
        CertaintyRung.Definitive => Chip.Definitive,
        CertaintyRung.Bounded => Chip.Bounded,
        _ => Chip.Unrecoverable,
    };

    public string DisplayPath => Model.ProvenancePath;

    public string Detail => Model.Rung == CertaintyRung.Definitive
        ? "Verified reconstruction — the captured key rebuilds the original at its location, replaced only after byte-for-byte verification."
        : Disposition switch
        {
            RestoreDisposition.Additive =>
                "Unchanged since the incident.",
            RestoreDisposition.DeletedSince =>
                "Deleted since the incident — restored as a new file.",
            RestoreDisposition.ModifiedSince =>
                "Modified since the incident — restoring to its original location discards newer content.",
            _ =>
                "A folder now occupies this path — it can't be restored to its original location.",
        };
}
