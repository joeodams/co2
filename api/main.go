package main

import (
	"crypto/rand"
	"database/sql"
	"embed"
	"encoding/hex"
	"fmt"
	"log"
	"math"
	"net/http"
	"os"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
	_ "modernc.org/sqlite"
)

//go:embed static/*
var embeddedStaticFiles embed.FS

var dashboardHTML = readGraphingHtmlFile("static/index.html")

type airQualityRecord struct {
	ID               string    `json:"id"`
	Timestamp        time.Time `json:"timestamp"`
	CO2              int       `json:"co2"`
	RelativeHumidity float64   `json:"rh"`
	Temperature      float64   `json:"temp"`
	Pressure         float64   `json:"pressure"`
}

const maxJSONBodyBytes int64 = 4096

func main() {
	dbPath := getenvDefault("CO2_DB_PATH", "airquality.db")
	listenAddr := getenvDefault("CO2_LISTEN_ADDR", "localhost:8080")
	apiKey := os.Getenv("CO2_API_KEY")

	// open the sqlite DB
	db, err := sql.Open("sqlite", dbPath)
	if err != nil {
		log.Fatal(err)
	}

	// this defers closing the connection until main() returns
	defer db.Close()

	db.SetMaxOpenConns(1)

	// re-initialise the DB on every startup in case we're starting the DB again
	if err := initDB(db); err != nil {
		log.Fatal(err)
	}

	router := newRouter(db, apiKey)

	log.Printf("Using database %s", dbPath)
	log.Printf("Listening on %s", listenAddr)
	if err := router.Run(listenAddr); err != nil {
		log.Fatal(err)
	}
}

func initDB(db *sql.DB) error {
	const schema = `
CREATE TABLE IF NOT EXISTS air_quality_records (
    id        TEXT PRIMARY KEY,
    timestamp TEXT NOT NULL,
    co2       INTEGER NOT NULL,
    rh        REAL NOT NULL,
    temp      REAL NOT NULL,
    pressure  REAL NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_air_quality_records_timestamp
    ON air_quality_records (timestamp);`

	if _, err := db.Exec(schema); err != nil {
		return err
	}

	return nil
}

func newRouter(db *sql.DB, apiKey string) *gin.Engine {

	// create a gin Engine object with default configurations
	router := gin.Default()

	// register the endpoints
	router.GET("/healthz", func(c *gin.Context) {
		c.JSON(http.StatusOK, gin.H{"status": "ok"})
	})
	router.GET("/", serveDashboardHandler())

	// /records api
	records := router.Group("/records")
	records.GET("", getLastMonthRecordsHandler(db))                                                               //this is public
	records.POST("", requireAPIKey(apiKey), limitRequestBody(maxJSONBodyBytes), postAirQualityRecordsHandler(db)) //this is private

	return router
}

func serveDashboardHandler() gin.HandlerFunc {
	return func(c *gin.Context) {
		c.Header("Cache-Control", "no-store")
		c.Data(http.StatusOK, "text/html; charset=utf-8", dashboardHTML)
	}
}

func getLastMonthRecordsHandler(db *sql.DB) gin.HandlerFunc {
	return func(c *gin.Context) {
		cutoff := time.Now().AddDate(0, 0, -30).UTC().Format(time.RFC3339)

		rows, err := db.Query(`
			SELECT id, timestamp, co2, rh, temp, pressure
			FROM air_quality_records
			WHERE timestamp >= ?
			ORDER BY timestamp
		`, cutoff)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}
		defer rows.Close()

		var records []airQualityRecord

		for rows.Next() {
			var r airQualityRecord
			var ts string

			// .scan gets the row values into an airQualityRecord obj
			if err := rows.Scan(&r.ID, &ts, &r.CO2, &r.RelativeHumidity, &r.Temperature, &r.Pressure); err != nil {
				c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
				return
			}

			// parse the timestamp
			r.Timestamp, err = time.Parse(time.RFC3339, ts)
			if err != nil {
				c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
				return
			}

			records = append(records, r)
		}
		if err := rows.Err(); err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}

		c.IndentedJSON(http.StatusOK, records)
	}
}

func postAirQualityRecordsHandler(db *sql.DB) gin.HandlerFunc {
	return func(c *gin.Context) {
		var r airQualityRecord

		if err := c.BindJSON(&r); err != nil {
			c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
			return
		}
		initialiseRecord(&r)
		if err := validateRecord(r); err != nil {
			c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
			return
		}
		ts := r.Timestamp.UTC().Format(time.RFC3339)

		_, err := db.Exec(
			`INSERT INTO air_quality_records (id, timestamp, co2, rh, temp, pressure)
			 VALUES (?, ?, ?, ?, ?, ?)`,
			r.ID, ts, r.CO2, r.RelativeHumidity, r.Temperature, r.Pressure,
		)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}

		c.IndentedJSON(http.StatusCreated, r)
	}
}

func initialiseRecord(r *airQualityRecord) {
	if r.ID == "" {
		r.ID = newRecordID()
	}

	if r.Timestamp.IsZero() {
		r.Timestamp = time.Now().UTC()
		return
	}

	r.Timestamp = r.Timestamp.UTC()
}

func requireAPIKey(apiKey string) gin.HandlerFunc {
	return func(c *gin.Context) {
		if !isAuthorized(c, apiKey) {
			c.AbortWithStatusJSON(http.StatusUnauthorized, gin.H{"error": "unauthorized"})
			return
		}

		c.Next()
	}
}

func isAuthorized(c *gin.Context, apiKey string) bool {
	token := strings.TrimSpace(c.GetHeader("X-API-Key"))
	if token == "" {
		token = strings.TrimSpace(strings.TrimPrefix(c.GetHeader("Authorization"), "Bearer "))
	}

	return token == apiKey
}

func limitRequestBody(maxBytes int64) gin.HandlerFunc {
	return func(c *gin.Context) {
		c.Request.Body = http.MaxBytesReader(c.Writer, c.Request.Body, maxBytes)
		c.Next()
	}
}

// sanity-check the input
// maybe a sensor has died and is spewing crap
// let's save ourselves a db job
func validateRecord(r airQualityRecord) error {
	switch {
	case r.CO2 <= 0 || r.CO2 > 10000:
		return fmt.Errorf("co2 must be between 1 and 10000 ppm")
	case math.IsNaN(r.RelativeHumidity) || r.RelativeHumidity < 0 || r.RelativeHumidity > 100:
		return fmt.Errorf("rh must be between 0 and 100 percent")
	case math.IsNaN(r.Temperature) || r.Temperature < -40 || r.Temperature > 85:
		return fmt.Errorf("temp must be between -40 and 85 C")
	case math.IsNaN(r.Pressure) || math.IsInf(r.Pressure, 0):
		return fmt.Errorf("pressure must be a finite number")
	case r.Pressure != 0 && (r.Pressure < 300 || r.Pressure > 1200):
		return fmt.Errorf("pressure must be 0 or between 300 and 1200 hPa")
	default:
		return nil
	}
}

func newRecordID() string {
	var randomBytes [16]byte
	if _, err := rand.Read(randomBytes[:]); err != nil {
		return time.Now().UTC().Format("20060102T150405.000000000Z")
	}

	return hex.EncodeToString(randomBytes[:])
}

func getenvDefault(key string, fallback string) string {
	if value := strings.TrimSpace(os.Getenv(key)); value != "" {
		return value
	}

	return fallback
}

func readGraphingHtmlFile(path string) []byte {
	data, err := embeddedStaticFiles.ReadFile(path)
	if err != nil {
		panic(err)
	}

	return data
}
