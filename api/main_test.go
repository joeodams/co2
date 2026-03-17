package main

import (
	"database/sql"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/gin-gonic/gin"
	_ "modernc.org/sqlite"
)

func TestPostAirQualityRecordDefaults(t *testing.T) {
	gin.SetMode(gin.TestMode)

	db := openTestDB(t)
	defer db.Close()

	router := gin.New()
	router.POST("/records", postAirQualityRecordsHandler(db))

	req := httptest.NewRequest(
		http.MethodPost,
		"/records",
		strings.NewReader(`{"co2":700,"rh":42.5,"temp":20.5}`),
	)
	req.Header.Set("Content-Type", "application/json")

	recorder := httptest.NewRecorder()
	router.ServeHTTP(recorder, req)

	if recorder.Code != http.StatusCreated {
		t.Fatalf("expected status %d, got %d: %s", http.StatusCreated, recorder.Code, recorder.Body.String())
	}

	var got airQualityRecord
	if err := json.Unmarshal(recorder.Body.Bytes(), &got); err != nil {
		t.Fatalf("decode response: %v", err)
	}

	if got.ID == "" {
		t.Fatal("expected generated id")
	}
	if got.Timestamp.IsZero() {
		t.Fatal("expected generated timestamp")
	}
	if got.Pressure != 0 {
		t.Fatalf("expected default pressure 0, got %v", got.Pressure)
	}

	var (
		stored airQualityRecord
		ts     string
	)
	row := db.QueryRow(`SELECT id, timestamp, co2, rh, temp, pressure FROM air_quality_records`)
	if err := row.Scan(&stored.ID, &ts, &stored.CO2, &stored.RelativeHumidity, &stored.Temperature, &stored.Pressure); err != nil {
		t.Fatalf("scan stored record: %v", err)
	}

	parsedTS, err := time.Parse(time.RFC3339, ts)
	if err != nil {
		t.Fatalf("parse stored timestamp: %v", err)
	}
	stored.Timestamp = parsedTS

	if stored.ID != got.ID {
		t.Fatalf("expected stored id %q, got %q", got.ID, stored.ID)
	}
	if stored.CO2 != 700 || stored.RelativeHumidity != 42.5 || stored.Temperature != 20.5 {
		t.Fatalf("unexpected stored values: %+v", stored)
	}
}

func TestPostAirQualityRecordRequiresApiKeyWhenConfigured(t *testing.T) {
	gin.SetMode(gin.TestMode)

	db := openTestDB(t)
	defer db.Close()

	router := gin.New()
	router.Use(requireAPIKey("secret-key"))
	router.POST("/records", postAirQualityRecordsHandler(db))

	req := httptest.NewRequest(
		http.MethodPost,
		"/records",
		strings.NewReader(`{"co2":700,"rh":42.5,"temp":20.5}`),
	)
	req.Header.Set("Content-Type", "application/json")

	recorder := httptest.NewRecorder()
	router.ServeHTTP(recorder, req)

	if recorder.Code != http.StatusUnauthorized {
		t.Fatalf("expected status %d, got %d: %s", http.StatusUnauthorized, recorder.Code, recorder.Body.String())
	}

	req = httptest.NewRequest(
		http.MethodPost,
		"/records",
		strings.NewReader(`{"co2":700,"rh":42.5,"temp":20.5}`),
	)
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-API-Key", "secret-key")

	recorder = httptest.NewRecorder()
	router.ServeHTTP(recorder, req)

	if recorder.Code != http.StatusCreated {
		t.Fatalf("expected status %d, got %d: %s", http.StatusCreated, recorder.Code, recorder.Body.String())
	}
}

func TestGetRecordsRequiresApiKeyWhenConfigured(t *testing.T) {
	gin.SetMode(gin.TestMode)

	db := openTestDB(t)
	defer db.Close()

	router := gin.New()
	router.Use(requireAPIKey("secret-key"))
	router.GET("/records", getAirQualityRecordsHandler(db))

	req := httptest.NewRequest(http.MethodGet, "/records", nil)
	recorder := httptest.NewRecorder()
	router.ServeHTTP(recorder, req)

	if recorder.Code != http.StatusUnauthorized {
		t.Fatalf("expected status %d, got %d: %s", http.StatusUnauthorized, recorder.Code, recorder.Body.String())
	}
}

func TestPostAirQualityRecordRejectsInvalidPayload(t *testing.T) {
	gin.SetMode(gin.TestMode)

	db := openTestDB(t)
	defer db.Close()

	router := gin.New()
	router.POST("/records", postAirQualityRecordsHandler(db))

	req := httptest.NewRequest(
		http.MethodPost,
		"/records",
		strings.NewReader(`{"co2":0,"rh":120,"temp":20.5}`),
	)
	req.Header.Set("Content-Type", "application/json")

	recorder := httptest.NewRecorder()
	router.ServeHTTP(recorder, req)

	if recorder.Code != http.StatusBadRequest {
		t.Fatalf("expected status %d, got %d: %s", http.StatusBadRequest, recorder.Code, recorder.Body.String())
	}
}

func openTestDB(t *testing.T) *sql.DB {
	t.Helper()

	db, err := sql.Open("sqlite", ":memory:")
	if err != nil {
		t.Fatalf("open sqlite db: %v", err)
	}
	db.SetMaxOpenConns(1)

	if err := initDB(db); err != nil {
		db.Close()
		t.Fatalf("init sqlite db: %v", err)
	}

	return db
}
