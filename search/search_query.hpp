#pragma once
#include "search/intermediate_result.hpp"
#include "search/keyword_lang_matcher.hpp"
#include "search/mode.hpp"
#include "search/retrieval.hpp"
#include "search/search_trie.hpp"
#include "search/suggest.hpp"
#include "search/v2/rank_table_cache.hpp"

#include "indexer/ftypes_matcher.hpp"
#include "indexer/index.hpp"
#include "indexer/rank_table.hpp"

#include "geometry/rect2d.hpp"

#include "base/buffer_vector.hpp"
#include "base/cancellable.hpp"
#include "base/limited_priority_queue.hpp"
#include "base/string_utils.hpp"

#include "std/map.hpp"
#include "std/string.hpp"
#include "std/unordered_set.hpp"
#include "std/vector.hpp"


#define HOUSE_SEARCH_TEST
#define FIND_LOCALITY_TEST

#ifdef HOUSE_SEARCH_TEST
#include "search/house_detector.hpp"
#endif

#ifdef FIND_LOCALITY_TEST
#include "search/locality_finder.hpp"
#endif


class FeatureType;
class CategoriesHolder;

namespace coding
{
class CompressedBitVector;
}

namespace storage
{
class CountryInfoGetter;
}

namespace search
{
struct Locality;
struct Region;
struct SearchQueryParams;

namespace impl
{
  class FeatureLoader;
  class BestNameFinder;
  class PreResult2Maker;
  class DoFindLocality;
  class HouseCompFactory;
}

// TODO (@y): rename this class to QueryProcessor.
class Query : public my::Cancellable
{
public:
  // Maximum result candidates count for each viewport/criteria.
  static size_t const kPreResultsCount = 200;

  Query(Index & index, CategoriesHolder const & categories, vector<Suggest> const & suggests,
        storage::CountryInfoGetter const & infoGetter);

  // my::Cancellable overrides:
  void Reset() override;
  void Cancel() override;

  inline void SupportOldFormat(bool b) { m_supportOldFormat = b; }

  void Init(bool viewportSearch);

  /// @param[in]  forceUpdate Pass true (default) to recache feature's ids even
  /// if viewport is a part of the old cached rect.
  void SetViewport(m2::RectD const & viewport, bool forceUpdate);
  void SetRankPivot(m2::PointD const & pivot);
  inline string const & GetPivotRegion() const { return m_region; }
  inline void SetPosition(m2::PointD const & position) { m_position = position; }

  inline void SetMode(Mode mode) { m_mode = mode; }
  inline void SetSearchInWorld(bool b) { m_worldSearch = b; }

  /// Suggestions language code, not the same as we use in mwm data
  int8_t m_inputLocaleCode, m_currentLocaleCode;

  void SetPreferredLocale(string const & locale);
  void SetInputLocale(string const & locale);

  void SetQuery(string const & query);
  inline bool IsEmptyQuery() const { return (m_prefix.empty() && m_tokens.empty()); }

  /// @name Different search functions.
  //@{
  virtual void Search(Results & res, size_t resCount);
  virtual void SearchViewportPoints(Results & res);

  // Tries to generate a (lat, lon) result from |m_query|.
  void SearchCoordinates(Results & res) const;
  //@}

  // Get scale level to make geometry index query for current viewport.
  virtual int GetQueryIndexScale(m2::RectD const & viewport) const;

  virtual void ClearCaches();

  struct CancelException {};

  /// @name This stuff is public for implementation classes in search_query.cpp
  /// Do not use it in client code.
  //@{

  void InitParams(bool localitySearch, SearchQueryParams & params);

protected:
  enum ViewportID
  {
    DEFAULT_V = -1,
    CURRENT_V = 0,
    LOCALITY_V = 1,
    COUNT_V = 2     // Should always be the last
  };

  friend string DebugPrint(ViewportID viewportId);

  class RetrievalCallback : public Retrieval::Callback
  {
  public:
    RetrievalCallback(Index & index, Query & query, ViewportID id);

    // Retrieval::Callback overrides:
    void OnFeaturesRetrieved(MwmSet::MwmId const & id, double scale,
                             coding::CompressedBitVector const & features) override;

    void OnMwmProcessed(MwmSet::MwmId const & id) override;

  private:

    Index & m_index;
    Query & m_query;
    ViewportID m_viewportId;
    search::v2::RankTableCache m_rankTableCache;
  };

  friend class impl::FeatureLoader;
  friend class impl::BestNameFinder;
  friend class impl::PreResult2Maker;
  friend class impl::DoFindLocality;
  friend class impl::HouseCompFactory;

  void ClearQueues();

  int GetCategoryLocales(int8_t (&arr) [3]) const;
  template <class ToDo> void ForEachCategoryTypes(ToDo toDo) const;
  template <class ToDo> void ProcessEmojiIfNeeded(
      strings::UniString const & token, size_t ind, ToDo & toDo) const;

  using TMWMVector = vector<shared_ptr<MwmInfo>>;
  using TOffsetsVector = map<MwmSet::MwmId, vector<uint32_t>>;
  using TFHeader = feature::DataHeader;

  void SetViewportByIndex(TMWMVector const & mwmsInfo, m2::RectD const & viewport, size_t idx,
                          bool forceUpdate);
  void ClearCache(size_t ind);

  void AddPreResult1(MwmSet::MwmId const & mwmId, uint32_t featureId, uint8_t rank, double priority,
                     ViewportID viewportId = DEFAULT_V);

  template <class T> void MakePreResult2(vector<T> & cont, vector<FeatureID> & streets);

  /// @param allMWMs Deprecated, need to support old search algorithm.
  /// @param oldHouseSearch Deprecated, need to support old search algorithm.
  //@{
  void FlushHouses(Results & res, bool allMWMs, vector<FeatureID> const & streets);

  void FlushResults(Results & res, bool allMWMs, size_t resCount, bool oldHouseSearch);
  void FlushViewportResults(Results & res, bool oldHouseSearch);
  //@}

  void RemoveStringPrefix(string const & str, string & res) const;
  void GetSuggestion(string const & name, string & suggest) const;
  template <class T> void ProcessSuggestions(vector<T> & vec, Results & res) const;

  void SearchAddress(Results & res);

  /// Search for best localities by input tokens.
  /// @param[in]  pMwm  MWM file for World
  /// @param[out] res1  Best city-locality
  /// @param[out] res2  Best region-locality
  void SearchLocality(MwmValue const * pMwm, Locality & res1, Region & res2);

  void SearchFeatures();
  void SearchFeaturesInViewport(ViewportID viewportId);
  void SearchFeaturesInViewport(SearchQueryParams const & params, TMWMVector const & mwmsInfo,
                                ViewportID viewportId);

  /// Do search in a set of maps.
  void SearchInMwms(TMWMVector const & mwmsInfo, SearchQueryParams const & params,
                    ViewportID viewportId);

  void SuggestStrings(Results & res);
  void MatchForSuggestionsImpl(strings::UniString const & token, int8_t locale, string const & prolog, Results & res);

  void GetBestMatchName(FeatureType const & f, string & name) const;

  Result MakeResult(impl::PreResult2 const & r) const;
  void MakeResultHighlight(Result & res) const;

  Index & m_index;
  CategoriesHolder const & m_categories;
  vector<Suggest> const & m_suggests;
  storage::CountryInfoGetter const & m_infoGetter;

  string m_region;
  string m_query;
  buffer_vector<strings::UniString, 32> m_tokens;
  strings::UniString m_prefix;
  set<uint32_t> m_prefferedTypes;

#ifdef HOUSE_SEARCH_TEST
  strings::UniString m_house;
  vector<FeatureID> m_streetID;
  HouseDetector m_houseDetector;
#endif

#ifdef FIND_LOCALITY_TEST
  LocalityFinder m_locality;
#endif

  m2::RectD m_viewport[COUNT_V];
  m2::PointD m_pivot;
  m2::PointD m_position;
  Mode m_mode;
  bool m_worldSearch;
  Retrieval m_retrieval;

  /// @name Get ranking params.
  //@{
  /// @return Rect for viewport-distance calculation.
  m2::RectD const & GetViewport(ViewportID vID = DEFAULT_V) const;
  /// @return Control point for distance-to calculation.
  m2::PointD GetPosition(ViewportID vID = DEFAULT_V) const;
  //@}

  void SetLanguage(int id, int8_t lang);
  int8_t GetLanguage(int id) const;

  KeywordLangMatcher m_keywordsScorer;

  bool m_supportOldFormat;

  template <class TParam>
  class TCompare
  {
    using TFunction = function<bool(TParam const &, TParam const &)>;
    TFunction m_fn;

  public:
    TCompare() : m_fn(0) {}
    explicit TCompare(TFunction const & fn) : m_fn(fn) {}

    template <class T> bool operator() (T const & v1, T const & v2) const
    {
      return m_fn(v1, v2);
    }
  };

  using TQueueCompare = TCompare<impl::PreResult1>;
  using TQueue = my::limited_priority_queue<impl::PreResult1, TQueueCompare>;

  /// @name Intermediate result queues sorted by different criterias.
  //@{
public:
  enum
  {
    kQueuesCount = 2
  };

protected:
  // The values order should be the same as in
  // g_arrCompare1, g_arrCompare2 function arrays.
  enum
  {
    DISTANCE_TO_PIVOT,  // LessDistance
    FEATURE_RANK       // LessRank
  };
  TQueue m_results[kQueuesCount];
  size_t m_queuesCount;
  bool m_keepHouseNumberInQuery;
  //@}
};

}  // namespace search
