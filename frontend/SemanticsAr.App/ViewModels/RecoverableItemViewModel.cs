using CommunityToolkit.Mvvm.ComponentModel;
using SemanticsAr.Core.Domain;
using Chip = SemanticsAr.App.Controls.CertaintyRung;

namespace SemanticsAr.App.ViewModels;

public partial class RecoverableItemViewModel : ObservableObject
{
    public RecoverableItemViewModel(RecoverableItem model)
    {
        Model = model;
    }

    public RecoverableItem Model { get; }

    [ObservableProperty]
    private bool _isSelected;

    public Chip Rung => Model.Rung switch
    {
        CertaintyRung.Definitive => Chip.Definitive,
        CertaintyRung.Bounded => Chip.Bounded,
        _ => Chip.Unrecoverable,
    };

    public string DisplayPath => Model.ProvenancePath;

    public string Detail => Model.Rung == CertaintyRung.Definitive
        ? "Key captured — restored and verified before it replaces the file."
        : "Held copy — recoverable within a bounded, expiring window.";
}
