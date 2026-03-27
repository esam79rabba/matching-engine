# Reference Data Model — Draft

**Status**: Starter artifact — decision-independent where noted
**Source**: §17 Data, Audit, and Reporting; §12.4 Reference Data Service (frozen baseline)

---

## Entity Catalog

### 1. Instrument

| Field | Type | Description | Status |
|-------|------|-------------|--------|
| InstrumentId | uint32 | Venue-assigned unique ID | Frozen |
| Symbol | string(32) | Trading symbol | Frozen |
| ISIN | string(12) | ISO identifier (nullable for non-ISIN assets) | Frozen |
| InstrumentType | enum | EQUITY, ETF, BOND, FUTURE, OPTION, FX, COMMODITY, CARBON, DIGITAL | Frozen |
| CurrencyCode | string(3) | Quotation currency (ISO 4217) | Frozen |
| TickSizeTableId | uint32 | FK to tick-size table | Frozen |
| LotSize | decimal | Minimum quantity increment | Frozen |
| PricePrecision | uint8 | Decimal places for price | Frozen |
| PriceMultiplier | uint32 | Internal integer = price x multiplier | Frozen |
| QuantityPrecision | uint8 | Decimal places for quantity | Frozen |
| PartitionId | uint16 | Assigned matching partition | Frozen |
| MarketSegmentId | uint32 | FK to market segment | Frozen |
| SessionProfileId | uint32 | FK to session profile | Frozen |
| StaticPriceBandPct | decimal | Static band percentage | Frozen |
| DynamicPriceBandPct | decimal | Dynamic band percentage | Frozen |
| CircuitBreakerEnabled | boolean | Volatility auction enabled | Frozen |
| PriorityModel | enum | PRICE_TIME (FR-1 only) | Frozen |
| Status | enum | PRE_LISTED, LISTED, SUSPENDED, DELISTED | Frozen |
| ListingDate | date | First trading date | Frozen |
| ExpiryDate | date | Last trading date (derivatives, nullable) | Frozen |
| **UnderlyingId** | uint32 | FK to underlying (derivatives, nullable) | Frozen |

**Ownership**: Reference Data Service
**Lifecycle**: PRE_LISTED → LISTED → SUSPENDED ↔ LISTED → DELISTED

### 2. Participant

| Field | Type | Description | Status |
|-------|------|-------------|--------|
| ParticipantId | uint32 | Venue-assigned unique ID | Frozen |
| LegalName | string(256) | Registered entity name | Frozen |
| LEI | string(20) | Legal Entity Identifier | Frozen |
| MembershipType | enum | TRADING_MEMBER, CLEARING_MEMBER, MARKET_MAKER, SPONSOR | Frozen |
| Status | enum | PENDING, ACTIVE, SUSPENDED, TERMINATED | Frozen |
| JurisdictionCode | string(2) | ISO 3166-1 | Frozen |
| OnboardingDate | date | Membership start | Frozen |
| RiskTier | string(16) | Risk classification | Frozen |
| ClearingMemberId | uint32 | FK (nullable) | Frozen |
| FeeTierId | uint32 | FK to fee schedule | Frozen |

**Lifecycle**: PENDING → ACTIVE → SUSPENDED ↔ ACTIVE → TERMINATED

### 3. Account

| Field | Type | Description | Status |
|-------|------|-------------|--------|
| AccountId | uint32 | Venue-assigned | Frozen |
| ParticipantId | uint32 | FK to participant | Frozen |
| AccountType | enum | HOUSE, CLIENT, MARKET_MAKING | Frozen |
| ClearingAccountRef | string(64) | CCP reference | Frozen |
| CurrencyCode | string(3) | Primary currency | Frozen |
| Status | enum | ACTIVE, SUSPENDED, CLOSED | Frozen |

### 4. Session Profile

| Field | Type | Description | Status |
|-------|------|-------------|--------|
| SessionProfileId | uint32 | Unique ID | Frozen |
| Name | string(64) | e.g., "EQUITY_MAIN" | Frozen |
| Timezone | string(64) | IANA timezone | Frozen |
| Phases | array | Ordered list of phase definitions | Frozen |

**Phase Definition**:
| Field | Type | Description |
|-------|------|-------------|
| PhaseName | string | e.g., "Pre-Open", "Continuous" |
| StartTime | time | Local time |
| InstrumentState | enum | PRE_OPEN, AUCTION, CONTINUOUS, CLOSING_AUCTION, POST_CLOSE, CLOSED |
| EndMode | enum | FIXED, RANDOM |
| RandomRangeSeconds | uint16 | Random end window (0 if FIXED) |
| Duration | duration | Phase duration (nullable if time-triggered) |

### 5. Tick-Size Table

| Field | Type | Description | Status |
|-------|------|-------------|--------|
| TickSizeTableId | uint32 | Unique ID | Frozen |
| Name | string(64) | e.g., "LARGE_CAP_EQUITIES" | Frozen |
| Bands | array | Ordered list of price-range → tick-size entries | Frozen |

**Band Entry**: `{ PriceFrom: decimal, PriceTo: decimal, TickSize: decimal }`

### 6. Lot-Size Rule

| Field | Type | Description | Status |
|-------|------|-------------|--------|
| InstrumentId or GroupId | uint32 | Scope | Frozen |
| RoundLot | uint64 | Standard lot (e.g., 100 shares) | Frozen |
| OddLotAllowed | boolean | Accept sub-round-lot orders | Frozen |
| MinQuantity | uint64 | Minimum order quantity | Frozen |
| MaxQuantity | uint64 | Maximum order quantity | Frozen |
| QuantityPrecision | uint8 | Decimal places | Frozen |

### 7. Fee Schedule

| Field | Type | Description | Status |
|-------|------|-------------|--------|
| FeeTierId | uint32 | Unique ID | Frozen (structure) |
| Name | string(64) | e.g., "STANDARD_MAKER_TAKER" | Frozen |
| MakerFeeRate | decimal | Per-unit maker fee (may be negative = rebate) | **Provisional** — depends on fee model decision |
| TakerFeeRate | decimal | Per-unit taker fee | **Provisional** |
| VolumeTiers | array | Optional volume-based tiers | **Provisional** |

### 8. Holiday Calendar

| Field | Type | Description | Status |
|-------|------|-------------|--------|
| CalendarId | uint32 | Unique ID | Frozen |
| Name | string(64) | e.g., "CALENDAR_UK" | Frozen |
| Holidays | array | List of non-trading dates | **Provisional** — depends on jurisdiction (D-03) |

### 9. Market Segment

| Field | Type | Description | Status |
|-------|------|-------------|--------|
| MarketSegmentId | uint32 | Unique ID | Frozen |
| Name | string(64) | e.g., "MAIN_BOARD" | Frozen |
| Description | string(256) | Segment description | Frozen |
| DefaultSessionProfileId | uint32 | FK | Frozen |

### 10. Risk Limits (per participant per instrument group)

| Field | Type | Description | Status |
|-------|------|-------------|--------|
| ParticipantId | uint32 | FK | Frozen |
| InstrumentGroupId | uint32 | FK (or ALL) | Frozen |
| MaxOrderPrice_Pct | decimal | Fat-finger price band | Frozen |
| MaxOrderQty | uint64 | Max quantity per order | Frozen |
| MaxOrderNotional | decimal | Max notional per order | Frozen |
| MaxMessageRate | uint32 | Messages per second | Frozen |
| MaxOpenOrders | uint32 | Max resting orders | Frozen |
| KillSwitchEnabled | boolean | Always true for FR-1 | Frozen |

## Entity Relationships

```
Participant 1──N Account
Participant 1──1 FeeTier
Instrument N──1 TickSizeTable
Instrument N──1 MarketSegment
Instrument N──1 SessionProfile
MarketSegment N──1 SessionProfile
RiskLimits N──1 Participant
RiskLimits N──1 InstrumentGroup
```

## Provisional Fields (Blocked by Decisions)

| Entity | Field | Blocked By |
|--------|-------|:----------:|
| Fee Schedule | MakerFeeRate, TakerFeeRate | Fee model decision |
| Holiday Calendar | Specific dates | D-03 (jurisdiction) |
| Instrument | Specific instrument universe | D-02 (asset class) |
