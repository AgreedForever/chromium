// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_PREFERENCES_PREF_SERVICE_SYNCABLE_H_
#define COMPONENTS_SYNC_PREFERENCES_PREF_SERVICE_SYNCABLE_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/macros.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/pref_value_store.h"
#include "components/sync_preferences/pref_model_associator.h"
#include "components/sync_preferences/synced_pref_observer.h"

class OverlayUserPrefStore;

namespace service_manager {
class Connector;
}

namespace syncer {
class SyncableService;
}

namespace sync_preferences {

class PrefModelAssociatorClient;
class PrefServiceSyncableObserver;

// A PrefService that can be synced. Users are forced to declare
// whether preferences are syncable or not when registering them to
// this PrefService.
class PrefServiceSyncable : public PrefService {
 public:
  // You may wish to use PrefServiceFactory or one of its subclasses
  // for simplified construction.
  PrefServiceSyncable(
      PrefNotifierImpl* pref_notifier,
      PrefValueStore* pref_value_store,
      PersistentPrefStore* user_prefs,
      user_prefs::PrefRegistrySyncable* pref_registry,
      const PrefModelAssociatorClient* pref_model_associato_client,
      base::Callback<void(PersistentPrefStore::PrefReadError)>
          read_error_callback,
      bool async);
  ~PrefServiceSyncable() override;

  // Creates an incognito copy of the pref service that shares most pref stores
  // but uses a fresh non-persistent overlay for the user pref store and an
  // individual extension pref store (to cache the effective extension prefs for
  // incognito windows). |overlay_pref_names| is a list of preference names
  // whose changes will not be persisted by the returned incognito pref service.
  PrefServiceSyncable* CreateIncognitoPrefService(
      PrefStore* incognito_extension_pref_store,
      const std::vector<const char*>& overlay_pref_names,
      std::set<PrefValueStore::PrefStoreType> already_connected_types,
      service_manager::Connector* incognito_connector,
      service_manager::Connector* user_connector);

  // Returns true if preferences state has synchronized with the remote
  // preferences. If true is returned it can be assumed the local preferences
  // has applied changes from the remote preferences. The two may not be
  // identical if a change is in flight (from either side).
  //
  // TODO(albertb): Given that we now support priority preferences, callers of
  // this method are likely better off making the preferences they care about
  // into priority preferences and calling IsPrioritySyncing().
  bool IsSyncing();

  // Returns true if priority preferences state has synchronized with the remote
  // priority preferences.
  bool IsPrioritySyncing();

  // Returns true if the pref under the given name is pulled down from sync.
  // Note this does not refer to SYNCABLE_PREF.
  bool IsPrefSynced(const std::string& name) const;

  void AddObserver(PrefServiceSyncableObserver* observer);
  void RemoveObserver(PrefServiceSyncableObserver* observer);

  void RegisterMergeDataFinishedCallback(const base::Closure& callback);

  // TODO(zea): Have PrefServiceSyncable implement
  // syncer::SyncableService directly.
  syncer::SyncableService* GetSyncableService(const syncer::ModelType& type);

  // Do not call this after having derived an incognito or per tab pref service.
  void UpdateCommandLinePrefStore(PrefStore* cmd_line_store) override;

  void AddSyncedPrefObserver(const std::string& name,
                             SyncedPrefObserver* observer);
  void RemoveSyncedPrefObserver(const std::string& name,
                                SyncedPrefObserver* observer);

 protected:
  // Set the PrefModelAssociatorClient to use for that object during tests.
  void SetPrefModelAssociatorClientForTesting(
      const PrefModelAssociatorClient* pref_model_associator_client);

 private:
  friend class PrefModelAssociator;

  void AddRegisteredSyncablePreference(const std::string& path, uint32_t flags);

  // Invoked internally when the IsSyncing() state changes.
  void OnIsSyncingChanged();

  // Process a local preference change. This can trigger new SyncChanges being
  // sent to the syncer.
  void ProcessPrefChange(const std::string& name);

  // Create an |OverlayUserPrefStore| where the overlayed in-memory pref store
  // is accessed remotely through the pref service.
  OverlayUserPrefStore* CreateOverlayUsingPrefService(
      user_prefs::PrefRegistrySyncable* pref_registry,
      std::set<PrefValueStore::PrefStoreType> already_connected_types,
      service_manager::Connector* incognito_connector,
      service_manager::Connector* user_connector) const;

  // Whether CreateIncognitoPrefService() has been called to create a
  // "forked" PrefService.
  bool pref_service_forked_;

  PrefModelAssociator pref_sync_associator_;
  PrefModelAssociator priority_pref_sync_associator_;

  base::ObserverList<PrefServiceSyncableObserver> observer_list_;

  DISALLOW_COPY_AND_ASSIGN(PrefServiceSyncable);
};

}  // namespace sync_preferences

#endif  // COMPONENTS_SYNC_PREFERENCES_PREF_SERVICE_SYNCABLE_H_
