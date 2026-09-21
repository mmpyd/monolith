#pragma once

#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Templates/SubclassOf.h"

// ---------------------------------------------------------------------------
// Cross-version IK Rig solver access (UE 5.5 vs 5.6+).
//
// The IK Rig solver stack was redesigned between UE 5.5 and 5.6:
//
//   * 5.6+ : solvers are USTRUCTs (`FIKRigSolverBase`) stored in an
//            `FInstancedStruct` stack on the UIKRigDefinition. Per-bone settings
//            are USTRUCTs (`FIKRigBoneSettingsBase`) reached through the SOLVER
//            (`Solver->GetBoneSettingsType()` / `Solver->GetBoneSettings(Bone)`).
//   * 5.5  : solvers are UObjects (`UIKRigSolver*`). Per-bone settings are
//            UObjects reached through the CONTROLLER
//            (`Controller->GetBoneSettings(Bone, SolverIndex)` -> UObject*).
//
// Because `UClass` derives from `UStruct` and a `UObject*` is its own reflection
// memory, the reflective read/write code downstream is identical on both engines:
// it only needs a `(const UStruct* Type, void* Memory)` pair. This header
// normalises both models to that pair and keeps every version gate in one place;
// callers never see `FIKRigSolverBase*` vs `UIKRigSolver*`.
//
// This module is Editor-only, so the WITH_EDITOR-guarded solver accessors used
// below (UsesBoneSettings / GetBonesWithSettings) are always available here.
// ---------------------------------------------------------------------------

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
#include "Rig/Solvers/IKRigSolverBase.h"
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigController.h"
#else
#include "Rig/Solvers/IKRigSolver.h"
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigController.h"
#endif

class UIKRigDefinition;
class UIKRigController;

namespace MonolithIKRig
{
	/** A concrete reflectable value: its UStruct type + the live memory backing it.
	 *  On 5.6+ this is (UScriptStruct*, struct memory); on 5.5 it is (UClass*, UObject*).
	 *  Either way `FJsonObjectConverter::UStructToJsonObject(Type, Memory, ...)` and
	 *  `TFieldIterator<FProperty>(Type)` + `Prop->ContainerPtrToValuePtr<void>(Memory)`
	 *  behave identically. */
	struct FReflectable
	{
		const UStruct* Type = nullptr;
		void* Memory = nullptr;

		bool IsValid() const { return Type != nullptr && Memory != nullptr; }
	};

	// ----- Solver-level type enumeration (for add_ik_solver) -----------------

	/** Enumerate every instantiable native solver type. On 5.6+ these are the
	 *  native `FIKRigSolverBase`-derived UScriptStructs; on 5.5 the non-abstract
	 *  `UIKRigSolver`-derived UClasses. Both are returned as `UStruct*` so the
	 *  caller's name/identity resolution is version-agnostic. */
	FORCEINLINE void EnumerateSolverTypes(TArray<UStruct*>& OutTypes)
	{
		OutTypes.Reset();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		for (TObjectIterator<UStruct> It; It; ++It)
		{
			UScriptStruct* S = Cast<UScriptStruct>(*It);
			if (!S || !S->IsNative() || !S->IsChildOf(FIKRigSolverBase::StaticStruct()))
			{
				continue;
			}
			if (S == FIKRigSolverBase::StaticStruct())
			{
				continue; // skip the abstract base
			}
			OutTypes.Add(S);
		}
#else
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* C = *It;
			if (!C || !C->IsChildOf(UIKRigSolver::StaticClass()))
			{
				continue;
			}
			if (C == UIKRigSolver::StaticClass() || C->HasAnyClassFlags(CLASS_Abstract))
			{
				continue;
			}
			OutTypes.Add(C);
		}
#endif
	}

	/** Add a solver of the given (previously enumerated) type to the rig via its
	 *  controller. Returns the new solver stack index, or INDEX_NONE on failure. */
	FORCEINLINE int32 AddSolver(UIKRigController* Controller, UStruct* ResolvedType)
	{
		if (!Controller || !ResolvedType)
		{
			return INDEX_NONE;
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		return Controller->AddSolver(CastChecked<UScriptStruct>(ResolvedType));
#else
		return Controller->AddSolver(TSubclassOf<UIKRigSolver>(CastChecked<UClass>(ResolvedType)));
#endif
	}

	// ----- Solver-level reflection (for list / read of the solver itself) -----

	/** The solver at `Index` as a reflectable (type + memory), for dumping the
	 *  solver's OWN settings. On 5.6+: the FInstancedStruct's script-struct +
	 *  memory; on 5.5: the UIKRigSolver's class + the object itself. */
	FORCEINLINE FReflectable GetSolverReflectable(UIKRigDefinition* Asset, int32 Index)
	{
		FReflectable Out;
		if (!Asset)
		{
			return Out;
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		const TArray<FInstancedStruct>& Solvers = Asset->GetSolverStructs();
		if (Solvers.IsValidIndex(Index) && Solvers[Index].GetScriptStruct())
		{
			Out.Type = Solvers[Index].GetScriptStruct();
			// const_cast: reflection read paths take non-const void*; callers must
			// not mutate a solver's own settings through this (read-only usage).
			Out.Memory = const_cast<uint8*>(Solvers[Index].GetMemory());
		}
#else
		const TArray<UIKRigSolver*>& Solvers = Asset->GetSolverArray();
		if (Solvers.IsValidIndex(Index) && Solvers[Index])
		{
			Out.Type = Solvers[Index]->GetClass();
			Out.Memory = Solvers[Index];
		}
#endif
		return Out;
	}

	// ----- Solver "start"/root bone (name differs across versions) -----------

	/** Set the solver's root/start bone. 5.6+ `SetStartBone`; 5.5 `SetRootBone`. */
	FORCEINLINE bool SetSolverStartBone(UIKRigController* Controller, FName BoneName, int32 SolverIndex)
	{
		if (!Controller)
		{
			return false;
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		return Controller->SetStartBone(BoneName, SolverIndex);
#else
		return Controller->SetRootBone(BoneName, SolverIndex);
#endif
	}

	/** Read the solver's root/start bone. 5.6+ `GetStartBone`; 5.5 `GetRootBone`. */
	FORCEINLINE FName GetSolverStartBone(UIKRigController* Controller, int32 SolverIndex)
	{
		if (!Controller)
		{
			return NAME_None;
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		return Controller->GetStartBone(SolverIndex);
#else
		return Controller->GetRootBone(SolverIndex);
#endif
	}

	// ----- Per-bone settings (the reflective authoring surface) --------------

	/** Does the solver at `SolverIndex` support per-bone settings at all?
	 *  5.6+ `Solver->UsesCustomBoneSettings()`; 5.5 `Solver->UsesBoneSettings()`. */
	FORCEINLINE bool SolverUsesBoneSettings(UIKRigController* Controller, int32 SolverIndex)
	{
		if (!Controller)
		{
			return false;
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		FIKRigSolverBase* Solver = Controller->GetSolverAtIndex(SolverIndex);
		return Solver && Solver->UsesCustomBoneSettings();
#else
		UIKRigSolver* Solver = Controller->GetSolverAtIndex(SolverIndex);
		return Solver && Solver->UsesBoneSettings();
#endif
	}

	/** Does the given bone already carry settings in this solver? */
	FORCEINLINE bool SolverHasSettingsOnBone(UIKRigController* Controller, int32 SolverIndex, FName BoneName)
	{
		if (!Controller)
		{
			return false;
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		FIKRigSolverBase* Solver = Controller->GetSolverAtIndex(SolverIndex);
		return Solver && Solver->HasSettingsOnBone(BoneName);
#else
		// 5.5 has no solver-side HasSettingsOnBone; the controller returns the
		// per-bone settings object (or nullptr) directly.
		return Controller->GetBoneSettings(BoneName, SolverIndex) != nullptr;
#endif
	}

	/** All bones that carry settings in this solver. */
	FORCEINLINE void GetBonesWithSettings(UIKRigController* Controller, int32 SolverIndex, TSet<FName>& OutBones)
	{
		OutBones.Reset();
		if (!Controller)
		{
			return;
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		if (FIKRigSolverBase* Solver = Controller->GetSolverAtIndex(SolverIndex))
		{
			Solver->GetBonesWithSettings(OutBones);
		}
#else
		if (UIKRigSolver* Solver = Controller->GetSolverAtIndex(SolverIndex))
		{
			Solver->GetBonesWithSettings(OutBones);
		}
#endif
	}

	/** Create the per-bone settings entry for (solver, bone) if absent. */
	FORCEINLINE bool AddBoneSetting(UIKRigController* Controller, int32 SolverIndex, FName BoneName)
	{
		return Controller ? Controller->AddBoneSetting(BoneName, SolverIndex) : false;
	}

	/** Can this bone take settings in this solver? 5.6+ takes an out-error text;
	 *  5.5's controller overload is (bone, index) only. */
	FORCEINLINE bool CanAddBoneSetting(UIKRigController* Controller, int32 SolverIndex, FName BoneName, FText& OutError)
	{
		if (!Controller)
		{
			return false;
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		return Controller->CanAddBoneSetting(BoneName, SolverIndex, &OutError);
#else
		return Controller->CanAddBoneSetting(BoneName, SolverIndex);
#endif
	}

	/** The live per-bone settings for (solver, bone) as a reflectable (type + memory).
	 *  `AddBoneSetting` must have been called first if the entry may not exist yet.
	 *  5.6+: (Solver->GetBoneSettingsType(), Solver->GetBoneSettings(bone)).
	 *  5.5 : obj = Controller->GetBoneSettings(bone, idx); (obj->GetClass(), obj). */
	FORCEINLINE FReflectable GetBoneSettings(UIKRigController* Controller, int32 SolverIndex, FName BoneName)
	{
		FReflectable Out;
		if (!Controller)
		{
			return Out;
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		FIKRigSolverBase* Solver = Controller->GetSolverAtIndex(SolverIndex);
		if (Solver)
		{
			Out.Type = Solver->GetBoneSettingsType();
			Out.Memory = Solver->GetBoneSettings(BoneName);
		}
#else
		UObject* Settings = Controller->GetBoneSettings(BoneName, SolverIndex);
		if (Settings)
		{
			Out.Type = Settings->GetClass();
			Out.Memory = Settings;
		}
#endif
		return Out;
	}
}
