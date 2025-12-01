package main

import (
	"database/sql"
	"log"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
	_ "modernc.org/sqlite"
)

type airQualityRecord struct {
	ID               string    `json:"id"`
	Timestamp        time.Time `json:"timestamp"`
	CO2              int       `json:"co2"`
	RelativeHumidity float64   `json:"rh"`
	Temperature      float64   `json:"temp"`
	Pressure         float64   `json:"pressure"`
}

func main() {
	db, err := sql.Open("sqlite", "airquality.db")
	if err != nil {
		log.Fatal(err)
	}
	defer db.Close()

	if err := initDB(db); err != nil {
		log.Fatal(err)
	}

	router := gin.Default()
	router.GET("/records/last-month", getLastMonthRecordsHandler(db))
	router.GET("/records", getAirQualityRecordsHandler(db))
	router.POST("/records", postAirQualityRecordsHandler(db))

	if err := router.Run("localhost:8080"); err != nil {
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
);`

	if _, err := db.Exec(schema); err != nil {
		return err
	}

	return nil
}

func getLastMonthRecordsHandler(db *sql.DB) gin.HandlerFunc {
	return func(c *gin.Context) {
		// Last 30 days; swap to AddDate(0, -1, 0) if you want calendar month.
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

			if err := rows.Scan(&r.ID, &ts, &r.CO2, &r.RelativeHumidity, &r.Temperature, &r.Pressure); err != nil {
				c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
				return
			}

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

func getAirQualityRecordsHandler(db *sql.DB) gin.HandlerFunc {
	return func(c *gin.Context) {
		rows, err := db.Query(`SELECT id, timestamp, co2, rh, temp, pressure FROM air_quality_records`)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}
		defer rows.Close()

		var records []airQualityRecord

		for rows.Next() {
			var r airQualityRecord
			var ts string

			if err := rows.Scan(&r.ID, &ts, &r.CO2, &r.RelativeHumidity, &r.Temperature, &r.Pressure); err != nil {
				c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
				return
			}

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
		ts := r.Timestamp.UTC().Format(time.RFC3339) // "2025-03-10T09:25:00Z"

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
