using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public static class Decision  {
    // Функция прокидывания командира на умность по заданному параметру
	/// <summary>
	/// Тест на умное решение 0 - идеальное, 1 - хорошее, 2 - плохое 
	/// </summary>
	/// <returns>The brain test.</returns>
	/// <param name="param">Parameter.</param>
	static public int Off_brain_test (float param)
	{
		int res = Random.Range (0,100);

		if (res > param) 
		{
			return 0;
		}
		if (res > param / 2) {
			return 1;
		} else {
			return 2;
		}

	}

	// принимает решение из списка возможных, с их весами

    /// <summary>
    /// Возвращает номер принятого решения по списку
	/// <summary>
	static public int MyMakeDecision(List <des_point>x )
    {
        int res = 99999;
        int count = x.Count;
        float sum = 0;
        for (int i = 0; i < count; i++)
            sum += x[i].weight;
        // Варианты решения лежат  во временном массиве  их общий вес - 100, что позволяет использовать рандом
		List<des_point> y = new List<des_point>();
        for (int i = 0; i < count; i++)
			y.Add(new des_point(x[i].num_des, x[i].weight / sum * 100));
        sum = 0;
        int temp = Random.Range( 0, 1000) / 10;
        for (int i = 0; i < count; i++)
        {
            if (sum > temp)
            {
                res = i - 1;
                break;
            }
            else
            {
                sum += x[i].weight;
            }
        }
            return res;
    }
}