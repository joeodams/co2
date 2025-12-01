package main

import (
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
)

type airQualityRecord struct {
    ID                string     `json:"id"`
    Timestamp         time.Time  `json:"timestamp"`
    CO2               int        `json:"co2"`
    RelativeHumidity  float64    `json:"rh"`
    Temperature       float64    `json:"temp"`
    Pressure          float64    `json:"pressure"`
}


var testRecords = []airQualityRecord{
    {
        ID:        "test-001",
        Timestamp: time.Date(2025, time.March, 10, 9, 0, 0, 0, time.UTC),
        CO2:       742,
        RelativeHumidity: 41.8,
        Temperature:      20.3,
        Pressure:         1012.4,
    },
    {
        ID:        "test-002",
        Timestamp: time.Date(2025, time.March, 10, 9, 5, 0, 0, time.UTC),
        CO2:       815,
        RelativeHumidity: 43.1,
        Temperature:      20.6,
        Pressure:         1012.1,
    },
    {
        ID:        "test-003",
        Timestamp: time.Date(2025, time.March, 10, 9, 10, 0, 0, time.UTC),
        CO2:       689,
        RelativeHumidity: 39.7,
        Temperature:      20.0,
        Pressure:         1011.9,
    },
    {
        ID:        "test-004",
        Timestamp: time.Date(2025, time.March, 10, 9, 15, 0, 0, time.UTC),
        CO2:       902,
        RelativeHumidity: 45.2,
        Temperature:      21.1,
        Pressure:         1011.7,
    },
    {
        ID:        "test-005",
        Timestamp: time.Date(2025, time.March, 10, 9, 20, 0, 0, time.UTC),
        CO2:       621,
        RelativeHumidity: 38.4,
        Temperature:      19.8,
        Pressure:         1011.5,
    },
}

func main() {
    router := gin.Default()
    router.GET("/records", getAirQualityRecords)
    router.POST("/records", postAirQualityRecords)

    router.Run("localhost:8080")
}

// getAirQualityRecords responds with the list of all air quality records as JSON.
func getAirQualityRecords(c *gin.Context) {
    c.IndentedJSON(http.StatusOK, testRecords)
}

// postAirQualityRecords adds an air quality record from JSON received in the request body.
func postAirQualityRecords(c *gin.Context) {
    var newAirQualityRecord airQualityRecord

    // Call BindJSON to bind the received JSON to
    // newAlbum.
    if err := c.BindJSON(&newAirQualityRecord); err != nil {
        return
    }

    // Add the new album to the slice.
    testRecords = append(testRecords, newAirQualityRecord)
    c.IndentedJSON(http.StatusCreated, newAirQualityRecord)
}